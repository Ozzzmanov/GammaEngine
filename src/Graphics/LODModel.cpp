#include "LODModel.h"

void LODModel::AddLOD(std::shared_ptr<StaticModel> model, float maxDistance) {
    m_lods.push_back({ model, maxDistance * maxDistance }); // Храним квадрат дистанции
}

StaticModel* LODModel::GetModelForDistance(float distSq) const {
    if (m_lods.empty()) return nullptr;

    // Предполагаем, что лоды отсортированы (LOD0, LOD1, ...)
    for (const auto& lod : m_lods) {
        if (distSq < lod.distance) {
            return lod.model.get();
        }
    }
    // Если дальше всех лодов - возвращаем последний (или nullptr, если хотим culling)
    return m_lods.back().model.get();
}