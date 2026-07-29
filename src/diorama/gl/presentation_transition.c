#ifdef ENABLE_DIORAMA

#include <stddef.h>

#include "diorama/presentation_transition.h"

bool DioramaTransition_IsSameMap(const struct DioramaSceneSnapshot *left,
                                 const struct DioramaSceneSnapshot *right)
{
    return left != NULL && right != NULL
        && left->mapGeneration == right->mapGeneration
        && left->mapGroup == right->mapGroup
        && left->mapNum == right->mapNum
        && left->mapLayoutId == right->mapLayoutId;
}

enum DioramaPresentationDecision DioramaTransition_Classify(
    const struct DioramaSceneSnapshot *current, bool current3DReady,
    const struct DioramaSceneSnapshot *last3D, bool hasLast3D)
{
    if (current3DReady)
        return DIORAMA_PRESENT_3D;
    if (current != NULL && hasLast3D
     && (current->sceneKind == DIORAMA_SCENE_DIALOGUE
      || current->sceneKind == DIORAMA_SCENE_MENU)
     && DioramaTransition_IsSameMap(current, last3D))
        return DIORAMA_PRESENT_SOFT_2D;
    return DIORAMA_PRESENT_HARD_2D;
}

#endif
