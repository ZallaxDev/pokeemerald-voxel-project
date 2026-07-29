#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "diorama/presentation_transition.h"

int main(void)
{
    struct DioramaSceneSnapshot current;
    struct DioramaSceneSnapshot rendered;

    memset(&current, 0, sizeof(current));
    current.mapGeneration = 4;
    current.mapGroup = 0;
    current.mapNum = 9;
    current.mapLayoutId = 10;
    rendered = current;

    assert(DioramaTransition_Classify(&current, true, &rendered, true)
        == DIORAMA_PRESENT_3D);
    current.sceneKind = DIORAMA_SCENE_DIALOGUE;
    assert(DioramaTransition_Classify(&current, false, &rendered, true)
        == DIORAMA_PRESENT_SOFT_2D);
    current.sceneKind = DIORAMA_SCENE_MENU;
    assert(DioramaTransition_Classify(&current, false, &rendered, true)
        == DIORAMA_PRESENT_SOFT_2D);
    current.sceneKind = DIORAMA_SCENE_TRANSITION;
    assert(DioramaTransition_Classify(&current, false, &rendered, true)
        == DIORAMA_PRESENT_HARD_2D);
    current.sceneKind = DIORAMA_SCENE_BATTLE;
    assert(DioramaTransition_Classify(&current, false, &rendered, true)
        == DIORAMA_PRESENT_HARD_2D);
    current.sceneKind = DIORAMA_SCENE_DIALOGUE;
    current.mapGeneration++;
    assert(DioramaTransition_Classify(&current, false, &rendered, true)
        == DIORAMA_PRESENT_HARD_2D);
    current = rendered;
    assert(DioramaTransition_Classify(&current, false, &rendered, false)
        == DIORAMA_PRESENT_HARD_2D);
    assert(DioramaTransition_Classify(NULL, false, &rendered, true)
        == DIORAMA_PRESENT_HARD_2D);
    puts("diorama presentation transition tests passed");
    return 0;
}
