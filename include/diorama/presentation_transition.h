#ifndef GUARD_DIORAMA_PRESENTATION_TRANSITION_H
#define GUARD_DIORAMA_PRESENTATION_TRANSITION_H

#include <stdbool.h>

#include "diorama/scene_snapshot.h"

enum DioramaPresentationDecision
{
    DIORAMA_PRESENT_HARD_2D,
    DIORAMA_PRESENT_SOFT_2D,
    DIORAMA_PRESENT_3D,
};

bool DioramaTransition_IsSameMap(const struct DioramaSceneSnapshot *left,
                                 const struct DioramaSceneSnapshot *right);
enum DioramaPresentationDecision DioramaTransition_Classify(
    const struct DioramaSceneSnapshot *current, bool current3DReady,
    const struct DioramaSceneSnapshot *last3D, bool hasLast3D);

#endif
