#include "Assistant.h"
#include "LogicAnalysis.h"
#include "LogicRouting.h"
#include <iostream>

using namespace eda::logic;
using namespace eda::logic::ai;
using namespace std;

static void check(bool condition, const string &message) {
    if (!condition)
        throw runtime_error(message);
}
static Snapshot snapshot(Project p, set<string> selection = {}) {
    Simulator sim(p, p.main);
    sim.settle();
    return capture(p, p.main, "", selection, sim);
}
static Json completion(Json message) {
    message["role"] = "assistant";
    return {{"choices", Json::array({{{"message", message}}})}};
}
struct Run {
    Reply reply;
    Json result;
};
static Run call(const Snapshot &s, const string &tool, const Json &args) {
    Run run;
    atomic_bool cancelled{false};
    bool first = true;
    auto transport = [&](const Json &body) {
        if (first) {
            first = false;
            Json fn = {{"name", tool}, {"arguments", args.dump()}};
            Json request = {{"id", "one"}, {"function", fn}, {"type", "function"}};
            return completion({{"tool_calls", Json::array({request})}});
        }
        const auto &last = body.at("messages").back();
        check(last.at("tool_call_id") == "one", "tool result ID mismatch");
        run.result = Json::parse(last.at("content").get<string>());
        return completion({{"content", "done"}});
    };
    run.reply = converse(s, Json::array(), transport, cancelled);
    return run;
}
static void fullAdderLayout() {
    const auto expected = parseTruthCsv("A,B,Cin,|,SUM,COUT\n0,0,0,0,0\n0,0,1,1,0\n0,1,0,1,0\n0,1,1,0,1\n1,0,"
                                        "0,1,0\n1,0,1,0,1\n1,1,0,0,1\n1,1,1,1,1");
    auto baseline = synthesize(expected, "adder");
    auto &old = baseline.circuit("adder");
    set<string> ids;
    for (const auto &w : old.wires)
        ids.insert(w.id);
    check(rerouteWires(baseline, old, ids).failed.empty(), "full adder baseline failed");
    auto before = wireLayoutQuality(baseline, old);
    auto response =
        call(snapshot(Project{}), "propose_circuit", {{"name", "adder"}, {"truth_csv", expected.csv()}});
    check(response.reply.proposal.has_value(), "full adder generation failed");
    auto &draft = *response.reply.proposal;
    auto &c = draft.project.circuit(draft.definition);
    auto after = wireLayoutQuality(draft.project, c);
    cout << "Full adder crowded / total crossings: " << before.crowded << "/" << before.crossings << " -> "
         << after.crowded << "/" << after.crossings << "\n";
    check(after.crowded < before.crowded && after.crossings <= before.crossings,
          "full adder crossings regressed");
    check(truthTable(draft.project, c.name).rows == expected.rows, "layout changed full adder outputs");
    auto strokes = visibleWireStrokes({{0, 0}, {0, 20}, {20, 20}}, {{0, 16}, {0, 19}}, 50);
    check(strokes.size() == 4 && strokes[2].first.y < 20 && strokes[2].second.y == 20 &&
              strokes[1].first.y < strokes[1].second.y,
          "overlapping gaps erased the bend or middle stroke");
}
int main() {
    try {
        fullAdderLayout();
        auto project = halfAdder();
        const auto table = truthTable(project, "main");
        auto correct = project.circuit("main").wires.back();
        auto &wires = project.circuit("main").wires;
        wires.pop_back();
        auto s = snapshot(project);
        const auto original = serialize(s.project);
        auto issues = call(s, "get_diagnostics", Json::object());
        check(!issues.result.at("items").empty(), "missing floating pin diagnostics");
        auto pin = call(s, "inspect_circuit", {{"id", correct.b.object}});
        auto net = pin.result.at("items").at(0).at("pins").at(0).at("net");
        auto trace = call(s, "inspect_circuit", {{"net", net}});
        check(!trace.reply.references.empty(), "could not locate pin");
        Json op = {{"action", "add"},
                   {"a", {{"object", correct.a.object}, {"pin", correct.a.pin}}},
                   {"b", {{"object", correct.b.object}, {"pin", correct.b.pin}}}};
        Json args = {{"operations", Json::array({op})}, {"expected_csv", table.csv()}};
        auto repaired = call(s, "propose_edit", args);
        check(repaired.reply.proposal.has_value(), "open wire repair: " + repaired.result.dump());
        check(truthTable(repaired.reply.proposal->project, "main").rows == table.rows, "wrong truth table");
        check(serialize(s.project) == original, "mutated snapshot");
        op["a"]["pin"] = "invalid";
        Json bad = args;
        bad["operations"][0] = op;
        check(call(s, "propose_edit", bad).result.contains("error"), "accepted invalid pin");
        auto wrong = correct;
        wrong.a = wires.back().a;
        wires.push_back(wrong);
        s = snapshot(project);
        check(s.diagnostics.empty() && truthTable(project, "main").rows != table.rows,
              "fixture must be ERC clean but incorrect");
        args["operations"][0]["action"] = "reconnect";
        args["operations"][0]["wire"] = correct.id;
        auto fixed = call(s, "propose_edit", args);
        check(fixed.reply.proposal.has_value(), "missed functional repair");
        args["operations"][0]["a"]["object"] = wrong.a.object;
        check(call(s, "propose_edit", args).result.contains("error"),
              "accepted functionally incorrect repair");
        args.erase("expected_csv");
        check(call(s, "propose_edit", args).result.contains("error"), "missing spec accepted");
        args = {{"operations", Json::array({{{"action", "reroute"}, {"wire", correct.id}}})}};
        check(call(s, "propose_edit", args).result.contains("error"), "unselected reroute");
        s.selection.insert(correct.id);
        auto routed = call(s, "propose_edit", args);
        check(routed.reply.proposal.has_value(), "reroute failed");
        auto &after = routed.reply.proposal->project.circuit("main").wires;
        check(after.size() == wires.size(), "reroute removed wires");
        for (size_t i = 0; i < after.size(); ++i)
            check(after[i].a == wires[i].a && after[i].b == wires[i].b, "reroute changed endpoints");
        s.scope = "instance";
        check(call(s, "propose_edit", args).result.contains("error"), "instance write");
        auto blocked = project;
        auto &blockedCircuit = blocked.circuit("main");
        Part obstacle;
        obstacle.kind = "Register";
        obstacle.id = blocked.id("p");
        obstacle.at = blockedCircuit.part(wrong.b.object)->at;
        blockedCircuit.parts.push_back(obstacle);
        auto blockedSnapshot = snapshot(blocked, {correct.id});
        auto routeError = call(blockedSnapshot, "propose_edit", args);
        check(routeError.result.contains("error") && !routeError.reply.proposal, "blocked route accepted");
        check(serialize(blockedSnapshot.project) == serialize(blocked), "failed route changed project");
        project.circuit("main").parts.front().width = 2;
        s = snapshot(project);
        check(s.diagnostics.dump().find("WIDTH") != string::npos, "width mismatch hidden");
        check(!call(s, "inspect_circuit", {{"id", "p1"}}).result.at("items").empty(),
              "invalid net blocked queries");
        atomic_bool stopped{true};
        bool contacted = false;
        try {
            converse(
                s, Json::array(),
                [&](const Json &) {
                    contacted = true;
                    return Json{};
                },
                stopped);
        } catch (const runtime_error &) {
        }
        check(!contacted, "cancelled request sent");
        cout << "Assistant topology, diagnostics, repair, verification, routing and cancellation passed\n";
        return 0;
    } catch (const exception &e) {
        cerr << e.what() << "\n";
        return 1;
    }
}
