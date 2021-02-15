#include "floorplan.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>  // for max double value
#include <sstream>
#include <stdexcept>

#include "absl/container/flat_hash_map.h"
#include "ann_place.hpp"
#include "core/lgedgeiter.hpp"
#include "mathutil.hpp"

// This will be used to keep track of user's request for more output during layout.
constexpr bool verbose = false;

// Temporary local for crazy mirror reflection stuff.
constexpr int maxMirrorDepth = 20;

static bool   xReflect = false;
static double xLeft[maxMirrorDepth];
static double xRight[maxMirrorDepth];
static int    xMirrorDepth = 0;
static bool   yReflect     = false;
static double yBottom[maxMirrorDepth];
static double yTop[maxMirrorDepth];
static int    yMirrorDepth = 0;

absl::flat_hash_map<string, int> NameCounts;

int  Name2Count(const string& arg) { return ++NameCounts[arg]; }
void clearCount() { NameCounts.clear(); }

void FPContainer::pushMirrorContext(double startX, double startY) {
  if (xMirror) {
    if (xMirrorDepth == maxMirrorDepth)
      throw out_of_range("X Mirror Depth exceeds maximum allowed.");
    xReflect             = !xReflect;
    xLeft[xMirrorDepth]  = startX + x;
    xRight[xMirrorDepth] = startX + x + width;
    xMirrorDepth += 1;
  }
  if (yMirror) {
    if (xMirrorDepth == maxMirrorDepth)
      throw out_of_range("Y Mirror Depth exceeds maximum allowed.");
    yReflect              = !yReflect;
    yBottom[yMirrorDepth] = startY + y;
    yTop[yMirrorDepth]    = startY + y + height;
    yMirrorDepth += 1;
  }
}

void FPContainer::popMirrorContext() {
  if (xMirror) {
    xReflect = !xReflect;
    xMirrorDepth -= 1;
  }
  if (yMirror) {
    yReflect = !yReflect;
    yMirrorDepth -= 1;
  }
}

double FPObject::calcX(double startX) const {
  if (xReflect) {
    return xLeft[xMirrorDepth - 1] - (startX + x - xRight[xMirrorDepth - 1] + width);
  } else
    return startX + x;
}

double FPObject::calcY(double startY) const {
  if (yReflect) {
    return yTop[yMirrorDepth - 1] - (startY + y - yBottom[yMirrorDepth - 1] + height);
  } else
    return startY + y;
}

// Methods for the dummy component class to help with some IO.
ostream& operator<<(ostream& s, dummyComponent& c) {
  s << "Component: " << c.getName() << " is of type: " << Ntype::get_name(c.getType()) << "\n";
  return s;
}

dummyComponent::dummyComponent(Ntype_op typeArg) {
  type = typeArg;
  name = std::string(Ntype::get_name(type));
}

dummyComponent::dummyComponent(string nameArg) {
  type = Ntype_op::Sub;
  name = nameArg;
  for (int i = 1; i < static_cast<int>(Ntype_op::Last_invalid); i++) {
    if (nameArg == Ntype::get_name(static_cast<Ntype_op>(i))) {
      type = static_cast<Ntype_op>(i);
      break;
    }
  }
}

void dummyComponent::myPrint() { cout << "Component: " << getName() << " is of type: " << Ntype::get_name(getType()) << "\n"; }

// Methods for FPObject
FPObject::FPObject()
    : refCount(0)
    , x(0.0)
    , y(0.0)
    , width(0.0)
    , height(0.0)
    , area(0.0)
    , type(Ntype_op::Invalid)
    , name(Ntype::get_name(Ntype_op::Invalid))
    , hint(UnknownGeography)
    , count(1) {}

void FPObject::setSize(double widthArg, double heightArg) {
  width  = widthArg;
  height = heightArg;
  area   = width * height;
}

void FPObject::setLocation(double xArg, double yArg) {
  x = xArg;
  y = yArg;
}

void FPObject::outputHotSpotLayout(ostream& o, double startX, double startY) {
  o << getUniqueName() << "\t" << getWidth() / 1000 << "\t" << getHeight() / 1000 << "\t" << calcX(startX) / 1000 << "\t"
    << calcY(startY) / 1000 << "\n";
}

string FPObject::getUniqueName() const {
  if (name == " " || name == "")
    return name;
  else
    return name + std::to_string(Name2Count(name));
}

unsigned int FPObject::outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX, double startY) {
  bool found = false;

  //Tree_index child_idx = tree.get_first_child(tidx);
  Tree_index child_idx = tree.get_last_free(tidx, getType());
  while (child_idx != tree.invalid_index()) {
    Node* child = tree.ref_data(child_idx);

    // fmt::print("testing child node {} with parent hier ({}, {})\n", child->debug_name(), child->get_hidx().level,
    // child->get_hidx().pos);
    if (child->get_type_op() != getType() || child->has_place()) {
      child_idx = tree.get_sibling_next(child_idx);
      continue;
    }

    found = true;
    if (verbose) {
      fmt::print("assigning child node {} to parent hier ({}, {})\n",
                 child->debug_name(),
                 child->get_hidx().level,
                 child->get_hidx().pos);
    }

    Ann_place p(calcX(startX), calcY(startY), getWidth(), getHeight());
    child->set_place(p);

    // set livehd name to floorplan node name so floorplan nodes can be easily identified by user later
    child->set_name(getUniqueName());

    tree.set_last_free(tidx, getType(), child_idx);

    break;
  }

  assert(found);

  return 1;
}

// Methods for the FPWrapper class.
FPCompWrapper::FPCompWrapper(dummyComponent* comp, double minAR, double maxAR, double areaArg, int countArg) {
  component      = comp;
  minAspectRatio = minAR;
  maxAspectRatio = maxAR;
  area           = areaArg;
  count          = countArg;
  name           = comp->getName();
  type           = comp->getType();
}

FPCompWrapper::FPCompWrapper(string nameArg, double xArg, double yArg, double widthArg, double heightArg) {
  dummyComponent* DC = new dummyComponent(nameArg);
  component          = DC;
  setLocation(xArg, yArg);
  setSize(widthArg, heightArg);
  minAspectRatio = width / height;
  maxAspectRatio = width / height;
  count          = 1;
  name           = DC->getName();
  type           = DC->getType();
}

FPCompWrapper::~FPCompWrapper() {
  // cout << "deleting component.\n";
  delete component;
}

double FPCompWrapper::ARInRange(double AR) {
  double maxAR = getMaxAR();
  if ((AR < 1 && maxAR > 1) || (AR > 1 && maxAR < 1))
    flip();
  maxAR         = getMaxAR();
  double minAR  = getMinAR();
  double retval = AR;
  if (maxAR < 1) {
    assert(false);  // should never happen?
    // retval = std::min(retval, minAR);
    // retval = std::max(retval, maxAR);
  } else {
    retval = std::max(retval, minAR);
    retval = std::min(retval, maxAR);
  }
  if (verbose)
    cout << "Target AR=" << AR << " minAR=" << minAR << " maxAR=" << maxAR << " returnAR=" << retval << "\n";

  if (AR < minAR || AR > maxAR) {
    cerr << "WARNING: requested AR " << AR << " is outside legal range (" << minAR << ", " << maxAR << "), using " << retval << "."
         << endl;
  }

  return retval;
}

void FPCompWrapper::flip() {
  double temp    = width;
  width          = height;
  height         = temp;
  minAspectRatio = 1 / minAspectRatio;
  maxAspectRatio = 1 / maxAspectRatio;

  if (minAspectRatio > maxAspectRatio) {
    double temp    = minAspectRatio;
    minAspectRatio = maxAspectRatio;
    maxAspectRatio = temp;
  }
}

bool FPCompWrapper::layout(FPOptimization opt, double ratio) {
  (void)opt;
  // Make sure the ratio is within the stated constraints.
  ratio = ARInRange(ratio);
  // Calculate width height from area and AR.
  // We have w/h = ratio => w = ratio*h.
  // and w*h = area => ratio*h*h = area => h^2 = area/ratio => h = sqrt(area/ratio)
  height = sqrt(area / ratio);
  width  = area / height;
  return true;
}

// Methods for the FPcontainer class.

// Default constructor
FPContainer::FPContainer(unsigned int rsize) : items(), yMirror(false), xMirror(false) {
  items.reserve(rsize);  // avoid many memory reallocations due to vector resizing, if possible
}

FPContainer::~FPContainer() {
  // It's very important to only delete items when their refcount hits zero.
  // cout << "deleting container.\n";
  for (int i = 0; i < (int)items.size(); i++) {
    FPObject* item     = items[i];
    int       newCount = item->decRefCount();
    if (newCount == 0)
      delete item;
  }
}

// To properly handle refCount, we will only allow one method to actually add (or remove) items from the item list.

void FPContainer::addComponentAtIndex(FPObject* comp, int index) {
  if (index < 0 || index > (int)items.size())
    throw invalid_argument("Attempt to add item to Container at illegal index.");

  const int oldsize = items.size();
  items.resize(items.size() + 1);

  // See if we need to move things to open space.
  if (index < oldsize && oldsize > 0) {
    for (int i = oldsize; i > index; i--) {
      items[i] = items[i - 1];
    }
  }

  items[index] = comp;
  comp->incRefCount();
}

FPObject* FPContainer::removeComponentAtIndex(int index) {
  if (index < 0 || index >= (int)items.size())
    throw invalid_argument("Attempt to add item to Container at illegal index.");
  FPObject* comp = items[index];
  // Now fill in the hole.
  for (int i = index; i < (int)items.size() - 1; i++) {
    items[i] = items[i + 1];
  }

  items.resize(items.size() - 1);

  // Often this component is about to be added somewhere else.
  // So, if the refCount is now zero, don't handle it here.
  // If the caller doesn't put it somewhere else, they will have to delete it themselves.
  comp->decRefCount();
  return comp;
}

void FPContainer::replaceComponent(FPObject* comp, int index) {
  removeComponentAtIndex(index);
  addComponentAtIndex(comp, index);
}

void FPContainer::addComponent(FPObject* comp) { addComponentAtIndex(comp, items.size()); }

void FPContainer::addComponentToFront(FPObject* comp) { addComponentAtIndex(comp, 0); }

void FPContainer::addComponent(FPObject* comp, int countArg) {
  comp->setCount(countArg);
  addComponent(comp);
}

FPObject* FPContainer::addComponentCluster(Ntype_op type, int count, double area, double maxAspectRatio, double minAspectRatio) {
  // First generate a dummy component with the correct information.
  dummyComponent* comp = new dummyComponent(type);
  // We first need to create a wrapper for the component.
  FPCompWrapper* wrapComp = new FPCompWrapper(comp, minAspectRatio, maxAspectRatio, area, count);
  addComponent(wrapComp);
  return wrapComp;
}

FPObject* FPContainer::addComponentCluster(string name, int count, double area, double maxAspectRatio, double minAspectRatio) {
  // First generate a dummy component with the correct information.
  dummyComponent* comp = new dummyComponent(name);
  // We first need to create a wrapper for the component.
  FPCompWrapper* wrapComp = new FPCompWrapper(comp, minAspectRatio, maxAspectRatio, area, count);
  addComponent(wrapComp);
  return wrapComp;
}

FPObject* FPContainer::removeComponent(int index) { return removeComponentAtIndex(index); }

// We need to sort items by total area.
// There are so few items, just use bubble sort.
// We want a descending sort.
// We know this won't change item counts, so just it have at the item list.
void FPContainer::sortByArea() {
  int len = items.size();
  for (int i = 0; i < len; i++) {
    double maxArea      = -1;
    int    maxAreaIndex = 0;
    for (int j = i; j < len; j++) {
      double newArea = items[j]->getArea() * items[j]->getCount();
      if (newArea > maxArea) {
        maxArea      = newArea;
        maxAreaIndex = j;
      }
    }
    FPObject* temp      = items[i];
    items[i]            = items[maxAreaIndex];
    items[maxAreaIndex] = temp;
  }
}

double FPContainer::totalArea() {
  area          = 0;
  int itemCount = getComponentCount();
  for (int i = 0; i < itemCount; i++) {
    FPObject* item = getComponent(i);
    area += item->totalArea();
  }
  area *= count;
  return area;
}

unsigned int FPObject::findNode(Node_tree& tree, Tree_index tidx, double cX, double cY) {
  Ntype_op t = getType();

  unsigned int count;

  if (Ntype::is_synthesizable(t)) {  // leaf node - current hier structure is fine
    count = outputLGraphLayout(tree, tidx, cX, cY);
  } else if (t == Ntype_op::Sub) {  // Sub node - parameters need to be adjusted

    bool found = false;

    Tree_index child_idx = tree.get_first_child(tidx);
    while (child_idx != tree.invalid_index()) {
      auto child = tree.ref_data(child_idx);

      if (!child->is_type_sub_present() || child->has_place()) {
        child_idx = tree.get_sibling_next(child_idx);
        continue;
      }

      LGraph* child_lg = LGraph::open(tree.get_root_lg()->get_path(), child->get_type_sub());

      if (child_lg->get_name() != getName()) {
        child_idx = tree.get_sibling_next(child_idx);
        continue;
      }

      // fmt::print("assigning child subnode {} to parent hier ({}, {})\n", child->debug_name(), child->get_hidx().level,
      // child->get_hidx().pos);

      // write placement information to subnode as well
      Ann_place p(calcX(cX), calcY(cY), getWidth() / 1000, getHeight() / 1000);

      child->set_place(p);
      child->set_name(getUniqueName());

      count = outputLGraphLayout(tree, child_idx, cX, cY);
      found = true;

      break;
    }

    assert(found);
  } else if (t == Ntype_op::Invalid) {  // specific kind of layout - current hier structure is fine
    count = outputLGraphLayout(tree, tidx, cX, cY);
  } else {
    fmt::print("???\n");
    assert(false);
  }

  return count;
}

unsigned int FPContainer::outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX, double startY) {
  pushMirrorContext(startX, startY);

  unsigned int total = 0;

  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* obj = getComponent(i);
    total += obj->findNode(tree, tidx, x + startX, y + startY);
  }

  popMirrorContext();

  return total;
}

// Methods for the GridLayout class.
gridLayout::gridLayout(unsigned int rsize) : FPContainer(rsize) {
  type = Ntype_op::Invalid;
  name = "Grid";
}

bool gridLayout::layout(FPOptimization opt, double targetAR) {
  (void)opt;
  // We assume that a grid is a repeating unit of a single object.
  // However, that object can be either a leaf component or a container.
  if (getComponentCount() != 1) {
    cerr << "Attempt to layout a grid with other than one component.\n";
    return false;
  }
  double tarea   = totalArea();
  double theight = sqrt(tarea / targetAR);
  double twidth  = tarea / theight;
  if (verbose)
    cout << "Begin Grid Layout for " << getName() << ", TargetAR=" << targetAR << " My area=" << tarea << " Implied W=" << twidth
         << " H=" << theight << "\n";

  FPObject* obj   = getComponent(0);
  int       total = obj->getCount();

  // Calculate the grid size closest to the target AR.
  xCount = balanceFactors(total, targetAR);
  yCount = total / xCount;
  // We want the gridRatio to express the actual width to height.
  double gridRatio = ((double)xCount) / yCount;
  // Now see what we want as the component ratio.
  double ratio = targetAR / gridRatio;

  if (verbose) {
    cout << "In Grid Layout, xCount=" << xCount << " yCount=" << yCount << "\n";
    cout << "In Grid Layout, Asking my inferior for AR=" << ratio << "\n";
  }

  // Now layout whatever is below us.
  // We need to set the count to 1 to avoid double counting area.
  obj->setCount(1);
  obj->layout(opt, ratio);
  assert(obj->valid());
  obj->setCount(total);
  double compWidth  = obj->getWidth();
  double compHeight = obj->getHeight();

  // Now we can set our own width and height.
  width  = compWidth * xCount;
  height = compHeight * yCount;
  area   = width * height;
  if (verbose)
    cout << "At End Grid Layout, TargetAR=" << targetAR << " actualAR=" << width / height << "\n";

  return true;
}

void gridLayout::outputHotSpotLayout(ostream& o, double startX, double startY) {
  if (getComponentCount() != 1) {
    cerr << "Attempt to output a grid with other than one component.\n";
    return;
  }

  double    compWidth, compHeight;
  string    compName;
  FPObject* obj = getComponent(0);
  compWidth     = obj->getWidth();
  compHeight    = obj->getHeight();
  compName      = obj->getName();

  int    compCount = xCount * yCount;
  string GridName  = getUniqueName();
  o << "# " << GridName << " stats: X=" << calcX(startX) << ", Y=" << calcY(startY) << ", W=" << width << ", H=" << height
    << ", area=" << area << "mm²\n";
  o << "# start " << GridName << " " << Ntype::get_name(getType()) << " grid " << compCount << " " << xCount << " " << yCount
    << "\n";
  int compNum = 1;
  for (int i = 0; i < yCount; i++) {
    double cy = (i * compHeight) + y + startY;
    for (int j = 0; j < xCount; j++) {
      double cx = (j * compWidth) + x + startX;
      obj->outputHotSpotLayout(o, cx, cy);
      compNum += 1;
    }
  }
  o << "# end " << GridName << "\n";
}

unsigned int gridLayout::outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX, double startY) {
  if (getComponentCount() != 1) {
    throw std::invalid_argument("Attempt to output a grid with other than one component.\n");
  }

  double    compWidth, compHeight;
  string    compName;
  FPObject* obj = getComponent(0);
  compWidth     = obj->getWidth();
  compHeight    = obj->getHeight();
  compName      = obj->getName();

  int compNum = 1;

  Ntype_op t = obj->getType();

  unsigned int total = 0;

  for (int i = 0; i < yCount; i++) {
    double cy = (i * compHeight) + y + startY;
    for (int j = 0; j < xCount; j++) {
      double cx = (j * compWidth) + x + startX;

      total += obj->findNode(tree, tidx, cx, cy);

      compNum += 1;
    }
  }

  return total;
}

// Methods for Baglayout Class
bagLayout::bagLayout(unsigned int rsize) : FPContainer(rsize) {
  type   = Ntype_op::Invalid;
  name   = "Bag";
  locked = false;
}

bool bagLayout::layout(FPOptimization opt, double targetAR) {
  // If we are locked, don't layout.
  if (locked)
    return true;

  // Calculate our area, and the implied target width and height.
  area             = totalArea();
  double remHeight = sqrt(area / targetAR);
  double remWidth  = area / remHeight;
  double nextX     = 0;
  double nextY     = 0;

  if (verbose) {
    cout << "In BagLayout for " << getName() << ", Area=" << area << " Width= " << remWidth << " Height=" << remHeight
         << " Target AR=" << targetAR << "\n";
  }

  // Sort the components, placing them in decreasing order of size.
  sortByArea();
  int itemCount = getComponentCount();
  for (int i = 0; i < itemCount; i++) {
    FPObject* comp     = getComponent(i);
    double    compArea = comp->totalArea();

    // Take the largest component, place it in the min dimension.
    // Except for the last component which gets places in the max dimension.
    // First calculate the AR, and the rest fill follow.
    double AR
        = (remWidth > remHeight || i == itemCount - 1) ? (compArea / remHeight) / remHeight : remWidth / (compArea / remWidth);
    if (comp->getCount() > 1) {
      // We have more than one component of the same type, so let the grid layout do the work.
      gridLayout* GL = new gridLayout(1);
      GL->addComponent(comp);
      replaceComponent(GL, i);
      comp = GL;
    }
    comp->layout(opt, AR);
    assert(comp->valid());
    // Now we have the final component, we can set the location.
    comp->setLocation(nextX, nextY);
    // Prepare for the next round.
    if (remWidth > remHeight || i == itemCount - 1) {
      double compWidth = comp->getWidth();
      remWidth -= compWidth;
      nextX += compWidth;
    } else {
      double compHeight = comp->getHeight();
      remHeight -= compHeight;
      nextY += compHeight;
    }

    if (verbose)
      cout << " remWidth=" << remWidth << " remHeight=" << remHeight << " AR=" << AR << "\n";
  }

  recalcSize();

  if (verbose) {
    cout << "Total Container Width=" << width << " Height=" << height << " Area=" << width * height << " AR=" << width / height
         << "\n";
  }

  return true;
}

void bagLayout::recalcSize() {
  // Go through the layout components and find the containers width and height.
  width  = 0;
  height = 0;
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* comp = getComponent(i);
    width          = std::max(width, comp->getX() + comp->getWidth());
    height         = std::max(height, comp->getY() + comp->getHeight());
  }
  area = width * height;
}

void bagLayout::outputHotSpotLayout(ostream& o, double startX, double startY) {
  pushMirrorContext(startX, startY);
  int    itemCount = getComponentCount();
  string groupName;
  if (itemCount != 1) {
    groupName = getUniqueName();
    o << "# " << groupName << " stats: X=" << calcX(startX) << ", Y=" << calcY(startY) << ", W=" << width << ", H=" << height
      << ", area=" << area << "mm²\n";
    o << "# start " << groupName << " " << Ntype::get_name(getType()) << " bag " << itemCount << "\n";
  }
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* obj = getComponent(i);
    obj->outputHotSpotLayout(o, x + startX, y + startY);
  }
  if (itemCount != 1)
    o << "# end " << groupName << "\n";
  popMirrorContext();
}

void fixedLayout::morph(double xFactor, double yFactor) {
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* comp = getComponent(i);
    comp->setLocation(comp->getX() * xFactor, comp->getY() * yFactor);
    comp->setSize(comp->getWidth() * xFactor, comp->getHeight() * yFactor);
  }
}

// This will read in a hotspot file, and create a bag layout that is locked.
// NOTE: In general, the layout method on a bag layout manager would not have been able to create the resultant layout.
fixedLayout::fixedLayout(const char* filename, double scalingFactor) : bagLayout(1) {
  locked = true;

  ifstream in(filename);

  // cout << "In read of fixedLayout\n";
  while (true) {
    char first = in.peek();
    if (in.eof() || in.bad() || in.fail())
      break;
    if (first == '\n') {
      in.get();
      continue;
    }
    if (first == '#')  // || first == ' ')
    {
      char getChar;
      while (!in.eof()) {
        getChar = in.get();
        // cout << getChar;
        if (getChar == '\n')
          break;
      }
      continue;
    }
    // Now we should have a real line.
    string name;
    if (in.peek() == ' ')
      name = "";
    else
      in >> name;
    // cout << "name is'" << name << "'\n";
    double x, y, width, height;
    in >> width >> height >> x >> y;
    this->addComponent(new FPCompWrapper(name, x * 1000, y * 1000, width * 1000, height * 1000));
  }

  // Let's find out if the baseline x,y positions are at 0, 0.
  // If not, renormalize all the locations.
  double minX = std::numeric_limits<double>::max();
  double minY = std::numeric_limits<double>::max();
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* comp = getComponent(i);
    minX           = std::min(minX, comp->getX());
    minY           = std::min(minY, comp->getY());
  }
  if (minX != 0.0 || minY != 0) {
    for (int i = 0; i < getComponentCount(); i++) {
      FPObject* comp = getComponent(i);
      comp->setLocation(comp->getX() - minX, comp->getY() - minY);
    }
  }

  // Perform scaling if needed.
  // NOTE.  This needs to happen after location normalization to avoid wierd offset effects.
  if (scalingFactor != 1.0) {
    morph(scalingFactor, scalingFactor);
  }

  // Now that the inferiors are renormalized, we can calculate the container size.
  recalcSize();
}

bool fixedLayout::layout(FPOptimization opt, double targetAR) {
  (void)opt;
  // Compare targetAR to our originalAR to calculate x and y scaling factors.
  double currentAR = width / height;
  double xFactor   = sqrt(targetAR / currentAR);
  double yFactor   = 1.0 / xFactor;
  morph(xFactor, yFactor);
  width  = xFactor * width;
  height = yFactor * height;
  return true;
}

// Geographic Layout.
geogLayout::geogLayout(unsigned int rsize) : FPContainer(rsize) {
  type = Ntype_op::Invalid;
  name = "Geog";
}

// some geography hints only work with certain numbers of items
void geogLayout::checkHint(int count, GeographyHint hint) const {
  if ((hint == LeftRight || hint == LeftRightMirror || hint == LeftRight180 || hint == TopBottom || hint == TopBottomMirror
       || hint == TopBottom180)
      && count != 2) {
    throw std::invalid_argument("geography hint and component count are incompatible!");
  }
}

void geogLayout::addComponent(FPObject* comp, int count, GeographyHint hint) {
  checkHint(count, hint);
  comp->setHint(hint);
  comp->setType(Ntype_op::Sub);
  FPContainer::addComponent(comp, count);
}

FPObject* geogLayout::addComponentCluster(Ntype_op type, int count, double area, double maxARArg, double minARArg,
                                          GeographyHint hint) {
  checkHint(count, hint);
  FPObject* comp = FPContainer::addComponentCluster(type, count, area, maxARArg, minARArg);
  comp->setHint(hint);
  return comp;
}

FPObject* geogLayout::addComponentCluster(string name, int count, double area, double maxARArg, double minARArg,
                                          GeographyHint hint) {
  checkHint(count, hint);
  FPObject* comp = FPContainer::addComponentCluster(name, count, area, maxARArg, minARArg);
  comp->setHint(hint);
  return comp;
}

bool geogLayout::layout(FPOptimization opt, double targetAR) {
  // All this routine does is set up some data structures,
  //   and then call the helper to recursively do the layout work.
  // We will keep track of containers we make in a "stack".
  // We will also need to keep track of center items so they can be layed out last.

  // Assume no item will be more than split in two.
  int        itemCount    = getComponentCount();
  int        maxArraySize = itemCount * 2;
  FPObject** layoutStack  = new FPObject*[maxArraySize];
  for (int i = 0; i < maxArraySize; i++) layoutStack[i] = 0;
  FPObject** centerItems = new FPObject*[itemCount];
  for (int i = 0; i < itemCount; i++) centerItems[i] = 0;

  // Calculate the total area, and the implied target width and height.
  area             = totalArea();
  double remHeight = sqrt(area / targetAR);
  double remWidth  = area / remHeight;

  if (verbose)
    cout << "In geogLayout for " << getName() << ".  A=" << area << " W=" << remWidth << " H=" << remHeight << "\n";

  // Now do the real work.
  bool retval = layoutHelper(opt, remWidth, remHeight, 0, 0, layoutStack, 0, centerItems, 0);

  // By now, the item list should be empty.
  if (getComponentCount() != 0) {
    cerr << "Non empty item list after recursive layout in geographic layout.\n";
    cerr << "Remaining Component count=" << getComponentCount() << "\n";
    for (int i = 0; i < getComponentCount(); i++) cerr << "Component " << i << " is of type " << getComponent(i)->getName() << "\n";
    return false;
  }

  // Now install the list of containers as our items.
  // Now go through the layout components and find the containers width and height.
  width  = 0;
  height = 0;
  for (int i = 0; i < maxArraySize; i++) {
    FPObject* comp = layoutStack[i];
    if (!comp)
      break;
    FPContainer::addComponent(comp);
    width  = std::max(width, comp->getX() + comp->getWidth());
    height = std::max(height, comp->getY() + comp->getHeight());
  }
  area = width * height;

  delete[] centerItems;
  delete[] layoutStack;

  return retval;
}

bool geogLayout::layoutHelper(FPOptimization opt, double remWidth, double remHeight, double curX, double curY,
                              FPObject** layoutStack, int curDepth, FPObject** centerItems, int centerItemsCount) {
  int itemCount = getComponentCount();
  // Check for the end of the recursion.
  if (itemCount == 0 && centerItemsCount == 0)
    return true;

  // Check if it time to layout the center items.
  // For now we will just stick them in a bag layout, and then use all remaining area to lay it out.
  if (itemCount == 0 && centerItemsCount > 0) {
    // First find out if there are repeating units that need a grid to layout.
    // We will do this by finding the GCD of the counts in the collection of components.
    int gcd = centerItems[0]->getCount();
    for (int i = 1; i < centerItemsCount; i++) gcd = GCD(gcd, centerItems[i]->getCount());
    bagLayout* BL = new bagLayout(centerItemsCount);
    for (int i = 0; i < centerItemsCount; i++) {
      FPObject* item = centerItems[i];
      BL->addComponent(item, item->getCount() / gcd);
    }

    if (verbose)
      cout << "Laying out Center items.  Count=" << centerItemsCount << " GCD=" << gcd << "\n";

    // Use all remaining area.
    double       targetAR = remWidth / remHeight;
    FPContainer* FPLayout = BL;
    if (gcd > 1) {
      // Put the bag container into a grid container and lay it out.
      gridLayout* GL = new gridLayout(1);
      GL->addComponent(BL, gcd);
      FPLayout = GL;
    }
    // Not sure if we need to set the hint, but when I missed this for the grid below, it was a bug.
    FPLayout->setHint(Center);
    FPLayout->layout(opt, targetAR);
    assert(FPLayout->valid());
    if (verbose)
      cout << "Laying out Center item(s).  Current x and y are(" << curX << "," << curY << ")\n";
    FPLayout->setLocation(curX, curY);
    // Put the layout on the stack.
    // DO we really need to do this????
    layoutStack[curDepth] = FPLayout;
    return true;
  }

  // Start by pealing off the first componet cluster.
  // And getting the standard information about it.
  FPObject*     comp     = removeComponent(0);
  GeographyHint compHint = comp->getHint();

  // In this case, we will just save them up for now, and then lay them out last.
  if (compHint == Center) {
    centerItems[centerItemsCount] = comp;
    centerItemsCount += 1;
    layoutHelper(opt, remWidth, remHeight, curX, curY, layoutStack, curDepth, centerItems, centerItemsCount);
  }

  if (compHint == LeftRight || compHint == TopBottom || compHint == LeftRightMirror || compHint == TopBottomMirror
      || compHint == LeftRight180 || compHint == TopBottom180) {
    // Check if the count is a multiple of two, and if half will fit on a side.
    // If not, just put it in one side.
    // If so, split the component into two, and handle the opposite sides separately.
    // In order to handle the split in a general way (with nested layouts)
    //    we will have to put the component into a bag for each side.
    // TODO Put some quick calc here to see if we can fit with half the items
    // If not, don't split.
    if (comp->getCount() % 2 == 0) {
      int newCount = comp->getCount() / 2;
      comp->setCount(newCount);
      bagLayout* BL1 = new bagLayout(1);
      bagLayout* BL2 = new bagLayout(1);
      BL1->addComponent(comp);
      BL2->addComponent(comp);
      if (compHint == LeftRight || compHint == LeftRightMirror || compHint == LeftRight180) {
        BL1->setHint(Left);
        BL2->setHint(Right);
        if (compHint == LeftRightMirror)
          BL2->xMirror = true;
      } else if (compHint == TopBottom || compHint == TopBottomMirror || compHint == TopBottom180) {
        BL1->setHint(Top);
        BL2->setHint(Bottom);
        if (compHint == TopBottomMirror)
          BL2->yMirror = true;
      }
      if (compHint == TopBottom180 || compHint == LeftRight180) {
        BL2->xMirror = true;
        BL2->yMirror = true;
      }
      comp = BL1;
      addComponentToFront(BL2);
    } else if (compHint == LeftRight || compHint == LeftRightMirror || compHint == LeftRight180)
      comp->setHint(Left);
    else
      comp->setHint(Top);
    compHint = comp->getHint();
  }

  // Now handle left, right, top, bottom.
  double newX, newY;
  if (compHint == Left || compHint == Top || compHint == Right || compHint == Bottom) {
    // Stuff the comp into a grid, and see if we can layout it out in the desired shape.
    // TODO TODO This assumes a component has an area.  For a container, it will not have an area until it gets layed out.

    double totalArea = comp->totalArea();
    assert(totalArea > 0);

    if (verbose)
      cout << "In geog for " << comp->getName() << ", total component area=" << totalArea << "\n";

    double targetWidth, targetHeight;
    newX = curX;
    newY = curY;
    if (compHint == Left || compHint == Right) {
      targetHeight = remHeight;
      targetWidth  = totalArea / targetHeight;
    } else  // if (compHint == Top || compHint == Bottom)
    {
      targetWidth  = remWidth;
      targetHeight = totalArea / targetWidth;
    }
    if (compHint == Left)
      newX += targetWidth;
    if (compHint == Right)
      curX = curX + (remWidth - targetWidth);
    if (compHint == Top)
      curY = curY + (remHeight - targetHeight);
    if (compHint == Bottom)
      newY += targetHeight;

    double    targetAR = targetWidth / targetHeight;
    FPObject* FPLayout = comp;
    if (comp->getCount() > 1) {
      gridLayout* grid = new gridLayout(1);
      grid->addComponent(comp);
      grid->setHint(compHint);
      FPLayout = grid;
    }
    FPLayout->layout(opt, targetAR);
    assert(FPLayout->valid());
    FPLayout->setLocation(curX, curY);
    // Put the layout on the stack.
    layoutStack[curDepth] = FPLayout;
    if (compHint == Left || compHint == Right)
      remWidth -= FPLayout->getWidth();
    else if (compHint == Top || compHint == Bottom)
      remHeight -= FPLayout->getHeight();
    layoutHelper(opt, remWidth, remHeight, newX, newY, layoutStack, curDepth + 1, centerItems, centerItemsCount);
  } else if (compHint != Center) {
    cerr << "Hint is not any of the recognized hints.  Hint=" << compHint << "\n";
    cerr << "Component is of type " << Ntype::get_name(comp->getType()) << "\n";
  }

  // TODO: fixups here?

  return true;
}

void geogLayout::outputHotSpotLayout(ostream& o, double startX, double startY) {
  pushMirrorContext(startX, startY);
  string layoutName = getUniqueName();
  o << "# " << layoutName << " stats: X=" << calcX(startX) << ", Y=" << calcY(startY) << ", W=" << width << ", H=" << height
    << ", area=" << area << "mm²\n";
  o << "# start " << layoutName << " " << Ntype::get_name(getType()) << " geog " << getComponentCount() << "\n";
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* obj = getComponent(i);
    obj->outputHotSpotLayout(o, x + startX, y + startY);
  }
  o << "# end " << layoutName << "\n";
  popMirrorContext();
}

// Output Helpers.

ostream& outputHotSpotHeader(const char* filename) {
  // Reset the name to counts map for this output.
  NameCounts.clear();

  ofstream& out = *(new ofstream(filename));
  out << "# FloorPlan output from ArchFP: UVA's Rapid Prototyping FloorPlanner.\n";
  out << "# Formatted for Input to HotSpot.\n";
  out << "# Line Format: <unit-name>\\t<width>\\t<height>\\t<left-x>\\t<bottom-y>\n";
  out << "# Module Format: <start/end>\\t<name>\\t<type>\\t<items>\\t<grid width (if grid)>\\t<grid height (if grid)>\n";
  out << "# all dimensions are in meters\n";
  out << "# comment lines begin with a '#'\n";
  out << "# comments and empty lines are ignored\n\n";

  return out;
}

void outputHotSpotFooter(ostream& o) { delete (&o); }
