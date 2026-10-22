#include "LogicRouting.h"
#include <limits>
#include <numeric>
#include <queue>

namespace eda::logic {
namespace {
constexpr double epsilon = 1e-6, clearance = 10, laneSpacing = 10;
bool near(double a, double b) { return std::abs(a - b) < epsilon; }
bool same(Point a, Point b) { return near(a.x, b.x) && near(a.y, b.y); }
struct Rect {
    double l, t, r, b;
    std::string object;
    bool contains(Point p) const { return p.x > l + epsilon && p.x < r - epsilon && p.y > t + epsilon && p.y < b - epsilon; }
    bool cuts(Point a, Point end) const {
        if (near(a.y, end.y)) return a.y > t + epsilon && a.y < b - epsilon &&
            std::max(a.x, end.x) > l + epsilon && std::min(a.x, end.x) < r - epsilon;
        return a.x > l + epsilon && a.x < r - epsilon &&
            std::max(a.y, end.y) > t + epsilon && std::min(a.y, end.y) < b - epsilon;
    }
    bool touches(const Rect &other) const { return l <= other.r && r >= other.l && t <= other.b && b >= other.t; }
};
Rect body(const Project &p, const Part &part, double margin = clearance) {
    auto size = partSize(p, part);
    Point lo = size * -.5, hi = size * .5;
    if (part.kind == "Subcircuit")
        for (const auto &s : p.circuit(part.circuit).shapes) if (s.appearance) {
            const auto bounds = shapeBounds(s);
            lo.x = std::min(lo.x, bounds.first.x - s.stroke / 2);
            lo.y = std::min(lo.y, bounds.first.y - s.stroke / 2);
            hi.x = std::max(hi.x, bounds.second.x + s.stroke / 2);
            hi.y = std::max(hi.y, bounds.second.y + s.stroke / 2);
        }
    for (int turn = 0; turn < part.rotation; ++turn) {
        lo = {-lo.y, lo.x}; hi = {-hi.y, hi.x};
        if (lo.x > hi.x) std::swap(lo.x, hi.x);
        if (lo.y > hi.y) std::swap(lo.y, hi.y);
    }
    return {part.at.x + lo.x - margin, part.at.y + lo.y - margin,
            part.at.x + hi.x + margin, part.at.y + hi.y + margin, part.id};
}
struct Nets {
    std::map<std::string, int> keys;
    std::vector<int> parents;
    int key(const std::string &name) {
        auto found = keys.find(name);
        if (found != keys.end()) return found->second;
        int id = int(parents.size()); keys[name] = id; parents.push_back(id); return id;
    }
    int root(int id) { return parents[size_t(id)] == id ? id : parents[size_t(id)] = root(parents[size_t(id)]); }
    void join(int a, int b) { parents[size_t(root(a))] = root(b); }
    explicit Nets(const Circuit &c, const Wire *candidate = nullptr) {
        std::map<std::string, int> labels;
        for (const auto &n : c.nodes) {
            int id = key(n.id + ":");
            if (!n.label.empty()) {
                auto found = labels.find(n.label);
                if (found != labels.end()) join(id, found->second); else labels[n.label] = id;
            }
        }
        for (const auto &w : c.wires) join(key(w.a.key()), key(w.b.key()));
        if (candidate) join(key(candidate->a.key()), key(candidate->b.key()));
    }
    int net(const Endpoint &e) { return root(key(e.key())); }
};
struct Segment { Point a, b; std::string wire; int net; };
std::vector<Segment> segments(const Project &p, const Circuit &c, Nets &nets, const std::string &exclude = {}) {
    std::vector<Segment> result;
    for (const auto &w : c.wires) if (w.id != exclude) {
        const auto ps = wirePoints(p, c, w);
        const int net = nets.net(w.a);
        for (size_t i = 1; i < ps.size(); ++i) if (!same(ps[i - 1], ps[i]))
            result.push_back({ps[i - 1], ps[i], w.id, net});
    }
    return result;
}
bool parallelTooClose(Point a, Point b, const Segment &s) {
    if (near(a.y, b.y) && near(s.a.y, s.b.y))
        return std::abs(a.y - s.a.y) < laneSpacing - epsilon &&
            std::min(std::max(a.x, b.x), std::max(s.a.x, s.b.x)) >
            std::max(std::min(a.x, b.x), std::min(s.a.x, s.b.x)) + epsilon;
    if (near(a.x, b.x) && near(s.a.x, s.b.x))
        return std::abs(a.x - s.a.x) < laneSpacing - epsilon &&
            std::min(std::max(a.y, b.y), std::max(s.a.y, s.b.y)) >
            std::max(std::min(a.y, b.y), std::min(s.a.y, s.b.y)) + epsilon;
    return false;
}
std::optional<Point> crossing(Point a, Point b, Point c, Point d) {
    if (!near(a.y, b.y) || !near(c.x, d.x)) return {};
    const Point at{c.x, a.y};
    if (at.x <= std::min(a.x, b.x) + epsilon || at.x >= std::max(a.x, b.x) - epsilon ||
        at.y <= std::min(c.y, d.y) + epsilon || at.y >= std::max(c.y, d.y) - epsilon) return {};
    return at;
}
Point escape(const Project &p, const Circuit &c, const Endpoint &e) {
    Point at = endpoint(p, c, e);
    const auto *part = c.part(e.object);
    if (!part) return at;
    auto canonical = *part; canonical.rotation = 0;
    auto pp = ports(p, canonical);
    auto pin = std::find_if(pp.begin(), pp.end(), [&](const Port &q) { return q.id == e.pin; });
    if (pin == pp.end()) return at;
    Point normal{pin->output ? 1.0 : -1.0, 0};
    if (((part->kind == "Mux" || part->kind == "Demux") && e.pin == "S") ||
        (part->kind == "TriState" && e.pin == "EN")) normal = {0, 1};
    if (part->kind == "Subcircuit" && p.circuit(part->circuit).interfaceLayout.count(e.pin)) {
        auto size = partSize(p, canonical);
        if (std::abs(pin->at.y) / size.y > std::abs(pin->at.x) / size.x)
            normal = {0, pin->at.y < 0 ? -1.0 : 1.0};
        else normal = {pin->at.x < 0 ? -1.0 : 1.0, 0};
    }
    for (int turn = 0; turn < part->rotation; ++turn) normal = {-normal.y, normal.x};
    const auto rect = body(p, *part);
    if (normal.x > 0) at.x = std::max(std::ceil((rect.r + 1) / 10) * 10, std::ceil((at.x + 10) / 10) * 10);
    if (normal.x < 0) at.x = std::min(std::floor((rect.l - 1) / 10) * 10, std::floor((at.x - 10) / 10) * 10);
    if (normal.y > 0) at.y = std::max(std::ceil((rect.b + 1) / 10) * 10, std::ceil((at.y + 10) / 10) * 10);
    if (normal.y < 0) at.y = std::min(std::floor((rect.t - 1) / 10) * 10, std::floor((at.y - 10) / 10) * 10);
    return at;
}
void append(std::vector<Point> &ps, Point q) {
    if (!ps.empty() && same(ps.back(), q)) return;
    if (ps.size() > 1) {
        auto a = ps[ps.size() - 2], b = ps.back();
        if ((near(a.x, b.x) && near(b.x, q.x) && (b.y-a.y)*(q.y-b.y) >= 0) ||
            (near(a.y, b.y) && near(b.y, q.y) && (b.x-a.x)*(q.x-b.x) >= 0)) ps.pop_back();
    }
    ps.push_back(q);
}
struct Search {
    const std::vector<Rect> &obstacles;
    const std::vector<Segment> &lanes;
    int net;
    bool clear(Point a, Point b, const std::string &stubObject = {}) const {
        for (const auto &rect : obstacles) if (rect.object != stubObject && rect.cuts(a, b)) return false;
        for (const auto &s : lanes) if (s.net != net && parallelTooClose(a, b, s)) return false;
        return true;
    }
    std::vector<Point> run(Point a, Point b, double margin) const {
        if (same(a, b)) return {a};
        const Rect area{std::min(a.x,b.x)-margin,std::min(a.y,b.y)-margin,
                        std::max(a.x,b.x)+margin,std::max(a.y,b.y)+margin,{}};
        std::vector<double> xs{a.x,b.x,area.l,area.r}, ys{a.y,b.y,area.t,area.b};
        for (const auto &rect : obstacles) if (rect.touches(area)) {
            if (rect.l >= area.l && rect.l <= area.r) xs.push_back(rect.l);
            if (rect.r >= area.l && rect.r <= area.r) xs.push_back(rect.r);
            if (rect.t >= area.t && rect.t <= area.b) ys.push_back(rect.t);
            if (rect.b >= area.t && rect.b <= area.b) ys.push_back(rect.b);
        }
        for (const auto &s : lanes) if (s.net != net) {
            Rect rect{std::min(s.a.x,s.b.x)-laneSpacing,std::min(s.a.y,s.b.y)-laneSpacing,
                      std::max(s.a.x,s.b.x)+laneSpacing,std::max(s.a.y,s.b.y)+laneSpacing,{}};
            if (!rect.touches(area)) continue;
            if (near(s.a.x,s.b.x)) { xs.push_back(rect.l); xs.push_back(rect.r); }
            else { ys.push_back(rect.t); ys.push_back(rect.b); }
        }
        auto unique = [](std::vector<double> &v) {
            std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end(),near),v.end());
        };
        unique(xs); unique(ys);
        const size_t nx=xs.size(), ny=ys.size(), cells=nx*ny;
        if (cells>50000) return {};
        auto cell = [&](Point q) {
            auto x=std::lower_bound(xs.begin(),xs.end(),q.x-epsilon)-xs.begin();
            auto y=std::lower_bound(ys.begin(),ys.end(),q.y-epsilon)-ys.begin();
            return size_t(y)*nx+size_t(x);
        };
        auto point = [&](size_t id) { return Point{xs[id%nx],ys[id/nx]}; };
        const auto start=cell(a), goal=cell(b);
        std::vector<double> costs(cells*3,std::numeric_limits<double>::infinity());
        std::vector<size_t> previous(cells*3,size_t(-1));
        std::vector<signed char> blocked(cells,-1);
        auto invalid = [&](size_t id) {
            if(blocked[id]>=0)return blocked[id]!=0;
            bool hit=false;for(const auto &rect:obstacles)if(rect.contains(point(id))){hit=true;break;}
            blocked[id]=hit?1:0;return hit;
        };
        if(invalid(start)||invalid(goal))return {};
        struct Entry { double score,cost;size_t state;bool operator<(const Entry &o) const {
            if(score!=o.score)return score>o.score;return state>o.state; } };
        std::priority_queue<Entry> queue;
        costs[start*3]=0;queue.push({distance(a,b),0,start*3});
        size_t finish=size_t(-1),visited=0;
        while(!queue.empty() && visited++<150000) {
            const auto top=queue.top();queue.pop();if(top.cost>costs[top.state]+epsilon)continue;
            const size_t id=top.state/3, direction=top.state%3;
            if(id==goal){finish=top.state;break;}
            const auto here=point(id);const size_t x=id%nx,y=id/nx;
            std::vector<std::pair<size_t,size_t>> neighbors;
            if(x)neighbors.push_back({id-1,1});if(x+1<nx)neighbors.push_back({id+1,1});
            if(y)neighbors.push_back({id-nx,2});if(y+1<ny)neighbors.push_back({id+nx,2});
            for(auto next:neighbors) {
                if(invalid(next.first))continue;auto there=point(next.first);if(!clear(here,there))continue;
                double extra=distance(here,there)+(direction&&direction!=next.second?18:0);
                for(const auto &s:lanes)if(s.net!=net &&
                    (crossing(here,there,s.a,s.b)||crossing(s.a,s.b,here,there)))extra+=30;
                const auto state=next.first*3+next.second;const double cost=top.cost+extra;
                if(cost+epsilon>=costs[state])continue;
                costs[state]=cost;previous[state]=top.state;
                queue.push({cost+std::abs(there.x-b.x)+std::abs(there.y-b.y),cost,state});
            }
        }
        if(finish==size_t(-1))return {};
        std::vector<Point> result;
        for(auto state=finish;state!=size_t(-1);state=previous[state])result.push_back(point(state/3));
        std::reverse(result.begin(),result.end());return result;
    }
};
bool sameGeometry(const Project &a, const Part &pa, const Project &b, const Part &pb) {
    const auto ra=body(a,pa),rb=body(b,pb);
    if(!near(ra.l,rb.l)||!near(ra.t,rb.t)||!near(ra.r,rb.r)||!near(ra.b,rb.b)||pa.rotation!=pb.rotation)return false;
    const auto x=ports(a,pa),y=ports(b,pb);if(x.size()!=y.size())return false;
    for(size_t i=0;i<x.size();++i)if(x[i].id!=y[i].id||!same(x[i].at,y[i].at))return false;
    return true;
}
} // namespace

WireRoute routeWire(const Project &p, const Circuit &c, const Wire &w, const std::vector<Point> &via) {
    if(w.a==w.b)return {{},"起点和终点相同"};
    const auto a=endpoint(p,c,w.a),b=endpoint(p,c,w.b),ea=escape(p,c,w.a),eb=escape(p,c,w.b);
    Nets nets(c,&w);const int net=nets.net(w.a);const auto lanes=segments(p,c,nets,w.id);
    std::vector<Rect> obstacles;
    for(const auto &part:c.parts)if(part.kind!="Text")obstacles.push_back(body(p,part));
    for(const auto &node:c.nodes)if(nets.net({node.id,""})!=net)
        obstacles.push_back({node.at.x-6,node.at.y-6,node.at.x+6,node.at.y+6,node.id});
    const Search search{obstacles,lanes,net};
    if(!search.clear(a,ea,w.a.object)||!search.clear(eb,b,w.b.object))
        return {{},"引脚出线空间被元件或其他网络占用"};
    std::vector<Point> targets{ea};targets.insert(targets.end(),via.begin(),via.end());targets.push_back(eb);
    std::vector<Point> result{a};append(result,ea);
    for(size_t i=1;i<targets.size();++i) {
        std::vector<Point> path;
        for(double margin:{100.0,260.0,600.0}) {
            path=search.run(targets[i-1],targets[i],margin);if(!path.empty())break;
        }
        if(path.empty())return {{},"没有找到可避让的通道；请移动元件或调整途经点"};
        for(auto point:path)append(result,point);
    }
    append(result,b);
    if(result.size()<2)return {{},"起点和终点重合"};
    return {result,{}};
}

RerouteResult rerouteWires(const Project &p, Circuit &c, const std::set<std::string> &ids) {
    RerouteResult report;
    std::set<std::string> pending=ids,changedIds;
    // 同一次局部移动可能先释放其他线路占用的出线口，再重试此前被阻挡的关联线。
    for(int pass=0;pass<3&&!pending.empty();++pass) {
        std::set<std::string> failed;
        for(auto &w:c.wires)if(pending.count(w.id)) {
            const auto routed=routeWire(p,c,w);
            if(!routed.ok()){failed.insert(w.id);continue;}
            const auto old=wirePoints(p,c,w);bool changed=old.size()!=routed.points.size();
            if(!changed)for(size_t i=0;i<old.size();++i)if(!same(old[i],routed.points[i])){changed=true;break;}
            if(changed){w.bends.assign(routed.points.begin()+1,routed.points.end()-1);changedIds.insert(w.id);}
        }
        if(failed.size()==pending.size()){pending=std::move(failed);break;}
        pending=std::move(failed);
    }
    report.changed=changedIds.size();report.failed.assign(pending.begin(),pending.end());
    return report;
}

WireRoute moveWireSegment(const Project &p, const Circuit &c, const Wire &w,
                          const std::vector<Point> &original, size_t segment, Point delta) {
    if(segment==0||segment>=original.size())return {{},"无效的导线线段"};
    Point a=original[segment-1],b=original[segment];
    const bool horizontal=near(a.y,b.y);
    if(segment==1)a=escape(p,c,w.a);
    if(segment+1==original.size())b=escape(p,c,w.b);
    const Point offset=horizontal?Point{0,std::round(delta.y/10)*10}:Point{std::round(delta.x/10)*10,0};
    if(same(offset,{0,0}))return {original,{}};
    std::vector<Point> guides;
    for(size_t i=1;i+1<segment;++i)append(guides,original[i]);
    append(guides,a+offset);append(guides,b+offset);
    for(size_t i=segment+1;i+1<original.size();++i)append(guides,original[i]);
    return routeWire(p,c,w,guides);
}

RerouteResult rerouteChangedGeometry(Project &p, const Project &before) {
    RerouteResult total;
    for(auto &c:p.circuits) {
        auto old=std::find_if(before.circuits.begin(),before.circuits.end(),[&](const Circuit &q){return q.name==c.name;});
        if(old==before.circuits.end())continue;
        std::set<std::string> changed,affected;
        std::vector<Rect> moved;
        for(const auto &part:c.parts) {
            const auto *previous=old->part(part.id);
            if(!previous||!sameGeometry(p,part,before,*previous)) {
                changed.insert(part.id);if(part.kind!="Text")moved.push_back(body(p,part));
            }
        }
        for(const auto &node:c.nodes) {
            auto prev=std::find_if(old->nodes.begin(),old->nodes.end(),[&](const Junction &q){return q.id==node.id;});
            if(prev==old->nodes.end()||!same(prev->at,node.at))changed.insert(node.id);
        }
        if(changed.empty())continue;
        for(const auto &w:c.wires) {
            if(changed.count(w.a.object)||changed.count(w.b.object)){affected.insert(w.id);continue;}
            const auto ps=wirePoints(p,c,w);
            for(size_t i=1;i<ps.size();++i)for(const auto &rect:moved)
                if(rect.cuts(ps[i-1],ps[i]))affected.insert(w.id);
        }
        const auto result=rerouteWires(p,c,affected);total.changed+=result.changed;
        total.failed.insert(total.failed.end(),result.failed.begin(),result.failed.end());
    }
    return total;
}

std::map<std::string,std::vector<Point>> wireCrossings(const Project &p,const Circuit &c) {
    Nets nets(c);const auto ss=segments(p,c,nets);std::map<std::string,std::vector<Point>> result;
    for(size_t i=0;i<ss.size();++i)for(size_t j=i+1;j<ss.size();++j) {
        const auto &a=ss[i],&b=ss[j];if(a.net==b.net)continue;
        auto at=crossing(a.a,a.b,b.a,b.b);const auto *vertical=&b;
        if(!at){at=crossing(b.a,b.b,a.a,a.b);vertical=&a;}
        if(!at)continue;auto &points=result[vertical->wire];
        if(std::none_of(points.begin(),points.end(),[&](Point q){return same(q,*at);}))points.push_back(*at);
    }
    return result;
}
} // namespace eda::logic
