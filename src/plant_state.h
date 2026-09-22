#pragma once
#include "esp_err.h"

typedef enum DisplayState {
    EYE = 0,
    AUDIO_VIS = 1
} DisplayState;

typedef enum PlantState { 
    WATERED = 0, 
    HYDRATED = 1, 
    DRY = 2, 
    VERY_DRY = 3, 
    UNDEFINED = 4 
} PlantState;

typedef enum StateEdges { 
    EDGE_UV = 0, 
    EDGE_UD = 1, 
    EDGE_UH = 2, 
    EDGE_UW = 3, 
    EDGE_WH = 4, 
    EDGE_HW = 5, 
    EDGE_HD = 6, 
    EDGE_DV = 7, 
    EDGE_VW = 8,
    EDGE_NC = 9, // no change
} StateEdges;

PlantState plant_get_current_state();