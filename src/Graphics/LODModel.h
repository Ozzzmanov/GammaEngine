#pragma once
#include <vector>
#include <string>
#include <memory>
#include "StaticModel.h"

class LODModel {
public:
    struct LOD {
        std::shared_ptr<StaticModel> model;
        float distance; // До какой дистанции этот лод активен
    };

    void AddLOD(std::shared_ptr<StaticModel> model, float maxDistance);

    // Возвращает модель, подходящую под дистанцию
    StaticModel* GetModelForDistance(float distSq) const;

private:
    std::vector<LOD> m_lods;
};