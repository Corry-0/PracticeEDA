#include "DigitalEditor.h"
#include "LogicSymbols.h"
#include <cstring>
#include <wx/dcmemory.h>

namespace eda::logic {
void LogicEditor::routingResult(const RerouteResult &result) {
    routingNotice = result.failed.empty() ? std::string{} :
        std::to_string(result.failed.size()) + " 条导线空间不足，保留原路径；请移开重叠元件或调整走线";
}
void LogicEditor::rerouteSelected() {
    if(!scope.empty())throw std::runtime_error("请先进入定义编辑再重排导线");
    std::set<std::string> ids;
    for(const auto &w:circuit().wires)
        if(canvas->selected.count(w.id)||canvas->selected.count(w.a.object)||canvas->selected.count(w.b.object))ids.insert(w.id);
    if(ids.empty())throw std::runtime_error("请先选择导线，或选择需要调整连线的元件");
    canvas->cancel();const auto before=history.project;
    const auto result=rerouteWires(history.project,circuit(),ids);
    history.commit(before);routingResult(result);
    if(result.failed.empty())routingNotice="已重排 "+std::to_string(ids.size())+" 条选中或关联导线";
    rebuild(false);
}
std::vector<std::string> LogicCanvas::wireCandidates(Point at) const {
    updateWireDisplay();
    std::vector<std::pair<std::string,double>> hits;
    const auto &p=owner->history.project;const auto &c=owner->circuit();
    for(auto it=c.wires.rbegin();it!=c.wires.rend();++it) {
        const auto ps=wirePoints(p,c,*it);
        double closest=1e10;
        for(size_t i=1;i<ps.size();++i) {
            double d=segmentDistance(at,ps[i-1],ps[i]);
            if(std::abs(ps[i].x-ps[i-1].x)<1e-6) {
                auto found=crossingGaps.find(it->id);
                if(found!=crossingGaps.end())for(auto gap:found->second)
                    if(std::abs(gap.x-ps[i].x)<1e-6&&std::abs(at.y-gap.y)<FromDIP(4)/view.zoom) {
                        const double r=FromDIP(4)/view.zoom;
                        d=std::min(distance(at,gap+Point{0,-r}),distance(at,gap+Point{0,r}));
                    }
            }
            closest=std::min(closest,d);
        }
        if(closest<FromDIP(7)/view.zoom)hits.push_back({it->id,closest});
    }
    std::stable_sort(hits.begin(),hits.end(),[](const auto &a,const auto &b){return a.second<b.second;});
    std::vector<std::string> ids;for(const auto &candidate:hits)ids.push_back(candidate.first);
    return ids;
}
bool LogicCanvas::cycleWire(Point at,int direction) {
    if(appearance||start)return false;
    const auto ids=wireCandidates(at);if(ids.empty())return false;
    int index=direction>0?-1:0;
    if(selected.size()==1) {
        auto found=std::find(ids.begin(),ids.end(),*selected.begin());
        if(found!=ids.end())index=int(found-ids.begin());
    }
    index=(index+direction+int(ids.size()))%int(ids.size());
    selected={ids[size_t(index)]};
    owner->inspect();Refresh();
    return true;
}
bool LogicCanvas::beginWireDrag(Point at) {
    if(appearance||tool!="Select"||start||selected.size()!=1)return false;
    const auto &c=owner->circuit();const auto id=*selected.begin();
    for(const auto &w:c.wires)if(w.id==id) {
        const auto ps=wirePoints(owner->history.project,c,w);
        for(size_t i=1;i<ps.size();++i) {
            if(distance(ps[i-1],ps[i])<FromDIP(16)/view.zoom)continue;
            if(segmentDistance(at,ps[i-1],ps[i])>FromDIP(6)/view.zoom)continue;
            if(distance(at,ps[i-1])<FromDIP(9)/view.zoom||distance(at,ps[i])<FromDIP(9)/view.zoom)continue;
            before=owner->history.project;wireSegment=int(i);draggedWire=id;wireDragPoints=ps;
            wireDragDelta={1e10,1e10};down=at;dragging=true;
            if(!HasCapture())CaptureMouse();return true;
        }
    }
    return false;
}
void LogicCanvas::updateWireDrag(Point at) {
    const auto delta=snap(at-down,10);
    if(distance(delta,wireDragDelta)<.01)return;
    wireDragDelta=delta;
    auto &c=owner->circuit();
    auto found=std::find_if(c.wires.begin(),c.wires.end(),[&](const Wire &w){return w.id==draggedWire;});
    if(found==c.wires.end())return;
    auto result=moveWireSegment(owner->history.project,c,*found,wireDragPoints,size_t(wireSegment),delta);
    if(result.ok()) {
        found->bends.assign(result.points.begin()+1,result.points.end()-1);
        owner->routingNotice.clear();wireDisplayDirty=true;
    } else {
        owner->routingNotice="当前位置没有安全通道，保留上次有效走线";
        owner->status->SetLabel(U(owner->routingNotice));
    }
    Refresh();
}
const WireRoute &LogicCanvas::wirePreview() {
    Point at=snap(mouse,10);
    Endpoint finish;
    if(auto pin=pinAt(mouse))finish=*pin;
    else finish={"__routing_cursor__",""};
    auto candidates=wireCandidates(mouse);
    std::stable_sort(candidates.begin(),candidates.end(),[&](const std::string &a,const std::string &b){return selected.count(a)>selected.count(b);});
    if(finish.object=="__routing_cursor__"&&!candidates.empty())
        for(const auto &w:owner->circuit().wires)if(w.id==candidates.front()) {
            const auto ps=wirePoints(owner->history.project,owner->circuit(),w);
            for(size_t i=1;i<ps.size();++i)if(segmentDistance(mouse,ps[i-1],ps[i])<FromDIP(7)/view.zoom) {
                const auto delta=ps[i]-ps[i-1];const double length=delta.x*delta.x+delta.y*delta.y;
                const double t=length?std::clamp(((mouse.x-ps[i-1].x)*delta.x+(mouse.y-ps[i-1].y)*delta.y)/length,0.0,1.0):0;
                at=ps[i-1]+delta*t;break;
            }
            break;
        }
    const auto target=finish.key()+"|"+(candidates.empty()?std::string{}:candidates.front());
    if(!previewDirty&&distance(at,previewMouse)<.01&&previewTarget==target)return previewRoute;
    previewDirty=false;previewMouse=at;previewTarget=target;
    auto c=owner->circuit();
    if(finish.object=="__routing_cursor__") {
        c.nodes.push_back({finish.object,"",at});
        if(!candidates.empty()) {
            auto found=std::find_if(c.wires.begin(),c.wires.end(),[&](const Wire &w){return w.id==candidates.front();});
            if(found!=c.wires.end())c.wires.push_back({"__routing_preview_join__",finish,found->a,{}});
        }
    }
    previewRoute=routeWire(owner->history.project,c,{"__routing_preview__",*start,finish,{}},bends);
    return previewRoute;
}
void LogicCanvas::updateWireDisplay() const {
    if(wireDisplayDirty) {
        crossingGaps=wireCrossings(owner->history.project,owner->circuit());wireDisplayDirty=false;
    }
}
void LogicCanvas::drawWire(wxGraphicsContext &g,const Wire &wire,bool clean) {
    updateWireDisplay();
    const auto ps=wirePoints(owner->history.project,owner->circuit(),wire);
    const auto found=crossingGaps.find(wire.id);
    for(size_t i=1;i<ps.size();++i) {
        const auto a=ps[i-1],b=ps[i];
        std::vector<double> gaps;
        if(found!=crossingGaps.end()&&std::abs(a.x-b.x)<1e-6)
            for(auto point:found->second)if(std::abs(point.x-a.x)<1e-6&&point.y>std::min(a.y,b.y)+1e-6&&point.y<std::max(a.y,b.y)-1e-6)gaps.push_back(point.y);
        if(gaps.empty()){g.StrokeLine(a.x,a.y,b.x,b.y);continue;}
        std::sort(gaps.begin(),gaps.end());
        double from=std::min(a.y,b.y),last=std::max(a.y,b.y);
        const double gap=clean?4.0:FromDIP(4)/view.zoom;
        for(auto y:gaps) {
            const auto end=std::max(from,y-gap);
            if(end>from)g.StrokeLine(a.x,from,a.x,end);
            from=std::max(from,y+gap);
        }
        if(from<last)g.StrokeLine(a.x,from,a.x,last);
    }
}
void LogicCanvas::drawWireHandles(wxGraphicsContext &g) {
    if(appearance||tool!="Select"||start||selected.size()!=1)return;
    g.SetPen(wxPen(wxColour("#6366f1"),1/view.zoom));g.SetBrush(*wxWHITE_BRUSH);
    for(const auto &w:owner->circuit().wires)if(selected.count(w.id)) {
        const auto ps=wirePoints(owner->history.project,owner->circuit(),w);
        for(size_t i=1;i<ps.size();++i)if(distance(ps[i-1],ps[i])>FromDIP(24)/view.zoom) {
            const auto mid=(ps[i-1]+ps[i])*.5;const double r=FromDIP(3)/view.zoom;
            g.DrawRectangle(mid.x-r,mid.y-r,r*2,r*2);
        }
    }
}

void LogicEditor::runRoutingSmoke(const std::filesystem::path &folder) {
    auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
    auto add=[](Project &p,const std::string &kind,Point at,uint32_t value=0,int rotation=0) {
        Part part;part.id=p.id("p");part.kind=kind;part.at=at;part.value=value;part.rotation=rotation;
        p.circuit(p.main).parts.push_back(part);return part.id;
    };
    auto activate=[&](const Project &p) {
        canvas->cancel();history.reset(p);current=p.main;canvas->selected.clear();canvas->tool="Select";
        canvas->appearance=false;routingNotice.clear();rebuild(true,true);
        canvas->view.zoom=1;canvas->view.offset={30,30};
    };
    auto event=[&](wxEventType kind,Point at,bool held=false,bool alt=false) {
        const auto screen=canvas->view.screen(at);wxMouseEvent e(kind);
        e.SetPosition({int(std::lround(screen.x)),int(std::lround(screen.y))});e.SetLeftDown(held);e.SetAltDown(alt);
        if(kind==wxEVT_LEFT_DOWN)canvas->leftDown(e);
        else if(kind==wxEVT_LEFT_UP)canvas->leftUp(e);
        else canvas->motion(e);
    };
    auto image=[&] {
        const auto size=canvas->GetClientSize();wxBitmap bitmap(size.x,size.y,32);
        {wxMemoryDC dc(bitmap);dc.SetBackground(*wxWHITE_BRUSH);dc.Clear();
         std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
         require(bool(g),"routing smoke graphics unavailable");canvas->render(*g,size,true);}
        return bitmap.ConvertToImage();
    };
    auto saveImage=[&](const char *name) {require(image().SaveFile(wxString((folder/name).wstring()),wxBITMAP_TYPE_PNG),"routing preview save failed");};
    auto pathsSame=[](const std::vector<Point> &a,const std::vector<Point> &b) {
        if(a.size()!=b.size())return false;
        for(size_t i=0;i<a.size();++i)if(distance(a[i],b[i])>.01)return false;return true;
    };
    Project p;p.name="局部避让与旧线路调整";
    const auto sourceA=add(p,"Input",{100,100},1),sourceB=add(p,"Input",{200,100}),gate=add(p,"And",{400,200});
    const auto stableA=add(p,"Input",{100,500}),stableB=add(p,"Output",{600,500});
    const auto first=p.id("w"),second=p.id("w"),stable=p.id("w");
    auto &initial=p.circuit(p.main);
    initial.wires={{first,{sourceA,"Y"},{gate,"A"},{}},{second,{sourceB,"Y"},{gate,"B"},{}},
                   {stable,{stableA,"Y"},{stableB,"A"},{}}};
    p.validate();activate(p);
    const auto original=serialize(history.project);
    require(original==serialize(p),"opening an old project rerouted it automatically");
    const auto hitAt=Point{280,100};
    require(canvas->wireCandidates(hitAt).size()==2,"overlap candidates missing");
    event(wxEVT_LEFT_DOWN,hitAt);event(wxEVT_LEFT_UP,hitAt);
    require(canvas->selected==std::set<std::string>{second},"wire hit order disagrees with drawing order");
    event(wxEVT_LEFT_DOWN,hitAt,false,true);event(wxEVT_LEFT_UP,hitAt);
    require(canvas->selected==std::set<std::string>{first}&&!canvas->start,"Alt-click did not cycle overlapping wires");
    wxKeyEvent tab(wxEVT_KEY_DOWN);tab.m_keyCode=WXK_TAB;canvas->key(tab);
    require(canvas->selected==std::set<std::string>{second},"Tab did not cycle overlapping wires");
    require(serialize(history.project)==original,"candidate selection changed topology");
    canvas->selected={first};canvas->tool="Wire";canvas->route(hitAt);
    require(circuit().wires[1].a==initial.wires[1].a&&circuit().wires[1].b==initial.wires[1].b,
            "branch split ignored explicitly selected overlapping wire");
    canvas->cancel();canvas->tool="Select";
    require(serialize(history.project)==original,"branch cancellation changed overlap project");
    const auto netlist=simulator->netlist();const auto time=simulator->time(),pending=simulator->pending();
    const auto runtime=simulator.get();canvas->selected={first,second};rerouteSelected();
    const auto routed=serialize(history.project);
    require(routed!=original&&routingNotice.find("空间不足")==std::string::npos,"selected wires were not rerouted");
    require(circuit().wires[2].bends.empty(),"selected reroute modified an unrelated wire");
    require(simulator.get()==runtime&&simulator->time()==time&&simulator->pending()==pending&&simulator->netlist()==netlist,
            "pure wire reroute reset simulation or changed electrical nets");
    require(simulator->read({gate,"A"}).value==1&&simulator->read({gate,"B"}).value==0,"separated inputs changed values");
    saveImage("routing-separated-inputs.png");writeAtomic(folder/"routing-separated-inputs.logic.json",routed);
    undo();require(serialize(history.project)==original,"reroute undo failed");
    undo(true);require(serialize(history.project)==routed,"reroute redo failed");
    canvas->placing="Register";canvas->subcircuit.clear();canvas->place({340,500});
    require(!circuit().wires[2].bends.empty(),"placing an obstacle did not reroute the intersecting wire");
    require(pathsSame(circuit().wires[0].bends,deserialize(routed).circuit("main").wires[0].bends),"new obstacle moved unrelated routing");
    saveImage("routing-new-obstacle.png");undo();
    require(serialize(history.project)==routed,"obstacle placement and routing were not one undo transaction");
    canvas->selected={stable};const auto dragRuntime=simulator.get();const auto dragTime=simulator->time();
    event(wxEVT_LEFT_DOWN,{350,500});
    require(canvas->wireSegment>=0,"selected straight wire did not enter segment drag");
    event(wxEVT_MOTION,{350,550},true);event(wxEVT_LEFT_UP,{350,550});
    const auto adjusted=serialize(history.project);
    require(adjusted!=routed&&!circuit().wires[2].bends.empty(),"wire segment drag did not commit");
    require(simulator.get()==dragRuntime&&simulator->time()==dragTime&&simulator->netlist()==netlist,"wire segment drag reset simulation");
    saveImage("routing-manual-segment.png");
    undo();require(serialize(history.project)==routed,"segment drag undo failed");
    undo(true);require(serialize(history.project)==adjusted,"segment drag redo failed");
    canvas->selected={stable};event(wxEVT_LEFT_DOWN,{350,550});event(wxEVT_MOTION,{350,580},true);
    wxKeyEvent escapeKey(wxEVT_KEY_DOWN);escapeKey.m_keyCode=WXK_ESCAPE;canvas->key(escapeKey);
    require(serialize(history.project)==adjusted&&!canvas->dragging,"segment cancellation committed geometry");

    Project fresh;fresh.name="新建导线预览避让";
    const auto a=add(fresh,"Input",{100,160}),b=add(fresh,"Output",{500,160});add(fresh,"Register",{300,160});
    activate(fresh);const auto bytes=serialize(history.project);
    canvas->route(endpoint(history.project,circuit(),{a,"Y"}));
    canvas->mouse=endpoint(history.project,circuit(),{b,"A"});
    const auto preview=canvas->wirePreview();require(preview.ok(),"smart wire preview failed around obstacle");
    require(serialize(history.project)==bytes,"routing preview changed the project");
    canvas->route(canvas->mouse);
    require(circuit().wires.size()==1&&pathsSame(wirePoints(history.project,circuit(),circuit().wires.front()),preview.points),
            "committed route differs from smart preview");
    saveImage("routing-new-wire.png");

    Project crossingProject;crossingProject.name="不同网络交叉断口";
    const auto left=add(crossingProject,"Input",{100,250},1),right=add(crossingProject,"Output",{600,250});
    const auto top=add(crossingProject,"Input",{300,100},0,1),bottom=add(crossingProject,"Output",{300,400},0,1);
    auto &cross=crossingProject.circuit(crossingProject.main);
    cross.wires={{crossingProject.id("w"),{left,"Y"},{right,"A"},{}},
                 {crossingProject.id("w"),{top,"Y"},{bottom,"A"},{}}};
    activate(crossingProject);canvas->selected.clear();auto beforeOrder=image();
    require(canvas->wireCandidates({300,250}).front()==cross.wires[0].id,"crossing hit target ignores visible gap");
    std::reverse(circuit().wires.begin(),circuit().wires.end());canvas->wireDisplayDirty=true;
    const auto afterOrder=image();
    require(beforeOrder.GetSize()==afterOrder.GetSize()&&std::memcmp(beforeOrder.GetData(),afterOrder.GetData(),
            size_t(beforeOrder.GetWidth())*size_t(beforeOrder.GetHeight())*3)==0,"crossing appearance depends on drawing order");
    require(!canvas->crossingGaps.empty(),"different-net crossing was not drawn with a gap");
    saveImage("routing-crossing-gap.png");
    writeAtomic(folder/"ROUTING-PASS.txt",
        "Legacy load unchanged; new wire preview/commit obstacle avoidance; selected reroute and unrelated-route isolation; "
        "Alt/Tab overlap cycling and explicit branch targeting; segment drag/cancel/undo/redo; obstacle placement undo; "
        "different-net crossing gaps independent of paint order; netlist and live simulator preservation passed.\n");
    canvas->cancel();history.reset(halfAdder());current="main";canvas->selected.clear();routingNotice.clear();rebuild(true,true);
}
} // namespace eda::logic
