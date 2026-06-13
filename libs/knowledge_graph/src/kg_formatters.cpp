#include <merak/kg/kg_provider.hpp>

#include <sstream>

namespace merak::kg {

std::string KnowledgeGraphProvider::subgraph_to_markdown(const SubGraph& sg) {
    if (sg.relations.empty()) return "";
    std::ostringstream os;
    os << "## 角色关系\n\n";
    for (auto& r : sg.relations) {
        os << "- **" << r.source_name << "** → **" << r.target_name
           << "**: " << r.kind_cn
           << " (" << to_string(r.a_to_b_stance) << "/" << to_string(r.b_to_a_stance) << ")";
        if (!r.fact.empty()) os << " — " << r.fact;
        os << "\n";
    }
    return os.str();
}

std::string KnowledgeGraphProvider::neighbor_graph_to_markdown(const NeighborGraph& ng) {
    std::ostringstream os;
    os << "## " << ng.center_entity << " 的关系网络\n\n";
    for (auto& r : ng.relations) {
        os << "- " << r.source_name << " → " << r.target_name
           << ": " << r.kind_cn << "\n";
    }
    return os.str();
}

std::string KnowledgeGraphProvider::path_result_to_markdown(const PathResult& pr) {
    if (!pr.found) return "*未找到路径*\n";
    std::ostringstream os;
    for (size_t i = 0; i < pr.paths.size(); ++i) {
        os << "**路径 " << (i + 1) << "** ("
           << pr.paths[i].size() << " 跳): ";
        for (size_t j = 0; j < pr.paths[i].size(); ++j) {
            if (j > 0) os << " → ";
            os << pr.paths[i][j].source_name;
        }
        os << " → " << pr.paths[i].back().target_name << "\n";
    }
    return os.str();
}

} // namespace merak::kg
