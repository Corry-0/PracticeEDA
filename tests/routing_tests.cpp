#include "LogicRouting.h"
#include <iostream>
using namespace eda;
using namespace eda::logic;
namespace {
void check(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
std::string add(Project &p,const std::string &kind,Point at,int rotation=0) {
    Part part;part.id=p.id("p");part.kind=kind;part.at=at;part.rotation=rotation;
    p.circuit(p.main).parts.push_back(part);return part.id;
}
bool equal(Point a,Point b){return distance(a,b)<1e-5;}
bool samePath(const std::vector<Point> &a,const std::vector<Point> &b) {
    if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(!equal(a[i],b[i]))return false;return true;
}
void orthogonalPath(const Project &p,const Circuit &c,const Wire &w,const std::vector<Point> &points) {
    check(points.size()>=2&&equal(points.front(),endpoint(p,c,w.a))&&equal(points.back(),endpoint(p,c,w.b)),"anchors moved");
    for(size_t i=1;i<points.size();++i)
        check(std::abs(points[i].x-points[i-1].x)<1e-5||std::abs(points[i].y-points[i-1].y)<1e-5,"diagonal route");
}
bool bodyHit(const std::vector<Point> &path,const Part &part,Point size) {
    if(part.rotation%2)std::swap(size.x,size.y);
    const auto lo=part.at-size*.5,hi=part.at+size*.5;
    for(size_t i=1;i<path.size();++i) {
        auto a=path[i-1],b=path[i];
        if(std::abs(a.y-b.y)<1e-5&&a.y>lo.y+1e-5&&a.y<hi.y-1e-5&&
           std::min(a.x,b.x)<hi.x-1e-5&&std::max(a.x,b.x)>lo.x+1e-5)return true;
        if(std::abs(a.x-b.x)<1e-5&&a.x>lo.x+1e-5&&a.x<hi.x-1e-5&&
           std::min(a.y,b.y)<hi.y-1e-5&&std::max(a.y,b.y)>lo.y+1e-5)return true;
    }
    return false;
}
double overlap(const std::vector<Point> &a,const std::vector<Point> &b) {
    double result=0;
    for(size_t i=1;i<a.size();++i)for(size_t j=1;j<b.size();++j) {
        if(std::abs(a[i].y-a[i-1].y)<1e-5&&std::abs(b[j].y-b[j-1].y)<1e-5&&std::abs(a[i].y-b[j].y)<1e-5)
            result+=std::max(0.0,std::min(std::max(a[i].x,a[i-1].x),std::max(b[j].x,b[j-1].x))-
                                  std::max(std::min(a[i].x,a[i-1].x),std::min(b[j].x,b[j-1].x)));
        if(std::abs(a[i].x-a[i-1].x)<1e-5&&std::abs(b[j].x-b[j-1].x)<1e-5&&std::abs(a[i].x-b[j].x)<1e-5)
            result+=std::max(0.0,std::min(std::max(a[i].y,a[i-1].y),std::max(b[j].y,b[j-1].y))-
                                  std::max(std::min(a[i].y,a[i-1].y),std::min(b[j].y,b[j-1].y)));
    }
    return result;
}
void localRouting() {
    Project p;const auto a=add(p,"Input",{100,100}),b=add(p,"Input",{200,100}),gate=add(p,"And",{400,200});
    p.circuit(p.main).part(a)->value=1;
    auto &c=p.circuit(p.main);const auto w1=p.id("w"),w2=p.id("w");
    c.wires.push_back({w1,{a,"Y"},{gate,"A"},{}});
    c.wires.push_back({w2,{b,"Y"},{gate,"B"},{}});
    Simulator old(p,p.main);old.settle();const auto nets=old.netlist();
    const auto result=rerouteWires(p,c,{w1,w2});
    check(result.failed.empty()&&result.changed==2,"overlap example did not route");
    const auto one=wirePoints(p,c,c.wires[0]),two=wirePoints(p,c,c.wires[1]);
    check(overlap(one,two)<1e-5,"different networks share line segments");
    for(const auto &w:c.wires)for(const auto &part:c.parts)
        check(!bodyHit(wirePoints(p,c,w),part,partSize(p,part)),"route goes through a component");
    Simulator now(p,p.main);now.settle();check(now.netlist()==nets,"routing changed electrical netlist");
    check(now.read({gate,"A"}).value==1&&now.read({gate,"B"}).value==0,"independent inputs changed");
    check(serialize(deserialize(serialize(p)))==serialize(p),"routed bends did not persist");
    const auto stable=serialize(p);rerouteWires(p,c,{w1,w2});check(serialize(p)==stable,"routing is not deterministic/idempotent");

    auto before=p;c.part(a)->at={300,100};
    const auto untouched=c.wires[1].bends;
    const auto moved=rerouteChangedGeometry(p,before);check(moved.failed.empty(),"moved endpoint could not route");
    check(c.wires[1].bends.size()==untouched.size()&&samePath(c.wires[1].bends,untouched),"unaffected wire changed");
    check(!bodyHit(wirePoints(p,c,c.wires[0]),*c.part(a),partSize(p,*c.part(a))),"moved input is crossed by own wire");
    Project obstacle;auto oa=add(obstacle,"Input",{0,0}),ob=add(obstacle,"Output",{500,0});
    auto &oc=obstacle.circuit(obstacle.main);oc.wires.push_back({obstacle.id("w"),{oa,"Y"},{ob,"A"},{}});
    const auto originalObstacle=obstacle;add(obstacle,"Register",{250,0});
    const auto blocked=rerouteChangedGeometry(obstacle,originalObstacle);
    check(blocked.failed.empty()&&blocked.changed==1,"new obstacle did not trigger local reroute");
    check(!bodyHit(wirePoints(obstacle,oc,oc.wires[0]),oc.parts.back(),partSize(obstacle,oc.parts.back())),"route crosses new obstacle");

    Project retry;auto ra=add(retry,"Input",{100,100}),rb=add(retry,"Input",{200,100}),rg=add(retry,"And",{400,200});
    auto &rc=retry.circuit(retry.main);
    rc.wires={{retry.id("w"),{ra,"Y"},{rg,"A"},{}},{retry.id("w"),{rb,"Y"},{rg,"B"},{}}};
    const auto retryBefore=retry;rc.part(ra)->at={300,100};
    const auto repaired=rerouteChangedGeometry(retry,retryBefore);
    check(repaired.failed.empty(),"released terminal lane was not retried");
    for(const auto &wire:rc.wires)for(const auto &part:rc.parts)
        check(!bodyHit(wirePoints(retry,rc,wire),part,partSize(retry,part)),"locally rerouted wire still crosses a moved body");
}
void rotationsAndManual() {
    for(int rotation=0;rotation<4;++rotation) {
        Project p;auto a=add(p,"Input",{0,0},rotation),b=add(p,"Output",{300,200},rotation);
        Wire w{p.id("w"),{a,"Y"},{b,"A"},{}};auto &c=p.circuit(p.main);c.wires.push_back(w);
        const auto route=routeWire(p,c,w);check(route.ok(),route.failure);orthogonalPath(p,c,w,route.points);
        for(const auto &part:c.parts)check(!bodyHit(route.points,part,partSize(p,part)),"rotated port exits through body");
        const Point start=route.points[1]-route.points[0];
        check(rotation==0?start.x>0:rotation==1?start.y>0:rotation==2?start.x<0:start.y<0,"port exit direction ignored");
    }
    Project p;auto a=add(p,"Input",{0,0}),b=add(p,"Output",{400,0});auto &c=p.circuit(p.main);
    Wire w{p.id("w"),{a,"Y"},{b,"A"},{}};c.wires.push_back(w);
    auto routed=routeWire(p,c,w,{{120,100},{300,100}});check(routed.ok(),routed.failure);
    check(std::any_of(routed.points.begin(),routed.points.end(),[](Point q){return std::abs(q.y-100)<1e-5;}),"manual waypoints ignored");
    auto simple=routeWire(p,c,w);check(simple.ok(),simple.failure);
    auto moved=moveWireSegment(p,c,w,simple.points,1,{0,60});check(moved.ok(),moved.failure);
    orthogonalPath(p,c,w,moved.points);
    check(std::any_of(moved.points.begin(),moved.points.end(),[](Point q){return std::abs(q.y-60)<1e-5;}),"straight line cannot be dragged");
    for(const auto &part:c.parts)check(!bodyHit(moved.points,part,partSize(p,part)),"manual segment drag crosses terminal body");
    add(p,"Register",{0,0});const auto before=serialize(p);auto impossible=routeWire(p,c,w);
    check(!impossible.ok(),"trapped terminal silently routed through a body");
    check(serialize(p)==before,"failed routing mutated the project");
    for(int rotation=0;rotation<4;++rotation) {
        Project special;const auto source=add(special,"Input",{0,300}),mux=add(special,"Mux",{300,0},rotation);
        auto &sc=special.circuit(special.main);Wire select{special.id("w"),{source,"Y"},{mux,"S"},{}};
        const auto route=routeWire(special,sc,select);check(route.ok(),route.failure);
        const auto last=route.points.back()-route.points[route.points.size()-2];
        check(rotation==0?last.y<0:rotation==1?last.x>0:rotation==2?last.y>0:last.x<0,"bottom select terminal direction ignored");
    }
    Project custom;const auto cs=add(custom,"Input",{0,-200});
    Circuit unit;unit.name="unit";Part pin;pin.id=custom.id("p");pin.kind="Input";unit.parts.push_back(pin);
    unit.interfaceLayout[pin.id]={0,-140};Shape outline;outline.id=custom.id("s");outline.kind="Rectangle";
    outline.a={-90,-80};outline.b={90,80};outline.appearance=true;unit.shapes.push_back(outline);custom.circuits.push_back(unit);
    Part instance;instance.id=custom.id("p");instance.kind="Subcircuit";instance.circuit="unit";instance.at={300,100};
    custom.circuit(custom.main).parts.push_back(instance);
    const auto customRoute=routeWire(custom,custom.circuit(custom.main),{custom.id("w"),{cs,"Y"},{instance.id,pin.id},{}});
    check(customRoute.ok(),customRoute.failure);
    check(customRoute.points.back().y>customRoute.points[customRoute.points.size()-2].y,"custom pin outside body lost outward exit direction");
}
void branchesAndCrossings() {
    Project p;auto a=add(p,"Input",{0,0}),b=add(p,"Output",{400,0});auto &c=p.circuit(p.main);
    auto j=p.id("j");c.nodes.push_back({j,"",{180,0}});
    c.wires.push_back({p.id("w"),{a,"Y"},{j,""},{}});c.wires.push_back({p.id("w"),{j,""},{b,"A"},{}});
    const auto branch=add(p,"Output",{300,150});Wire w{p.id("w"),{j,""},{branch,"A"},{}};
    c.wires.push_back(w);check(routeWire(p,c,w).ok(),"explicit same-net branch cannot leave trunk");
    check(wireCrossings(p,c).empty(),"same-net branch drawn as disconnected crossing");
    Project x;auto &xc=x.circuit(x.main);
    xc.nodes={{"j1","",{0,100}},{"j2","",{300,100}},{"j3","",{150,0}},{"j4","",{150,200}}};
    xc.wires={{"w5",{"j1",""},{"j2",""},{}},{"w6",{"j3",""},{"j4",""},{}}};x.nextId=7;
    auto gaps=wireCrossings(x,xc);check(gaps["w6"].size()==1&&equal(gaps["w6"][0],{150,100}),"different-net crossing missing gap");
    xc.nodes[0].label="BUS";xc.nodes[2].label="BUS";
    check(wireCrossings(x,xc).empty(),"named aliases treated as different networks");
}
} // namespace
int main(){try{localRouting();rotationsAndManual();branchesAndCrossings();std::cout<<"Local routing, network spacing, component clearance, rotations, waypoints, segment editing, persistence and topology passed.\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
