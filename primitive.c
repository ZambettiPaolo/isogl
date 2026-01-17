#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h> 
#include "primitive.h"



int set_angle(double* angle, double new_angle){
    int turn;
    if (new_angle >=360 || new_angle <=-360 ){
        turn = (int) new_angle/360;
        *angle = new_angle - turn*360;
    }else{
        *angle = new_angle;
    }
    if (*angle <0) *angle = 360 + *angle;
    return 0;
}



int transform_point (Origin* origin, Point* p_rel, Point* p_abs){
    p_abs->x = origin->ori_abs.x + p_rel->x * origin->cos_angle_abs - p_rel->y * origin->sin_angle_abs;
    p_abs->y = origin->ori_abs.y + p_rel->x * origin->sin_angle_abs + p_rel->y * origin->cos_angle_abs;
    return 0;
}

int rigen_origin_abs(Origin* origin1, Origin* origin2){
    set_angle(&(origin2->angle_abs), (origin1->angle_abs + origin2->angle));
    origin2->cos_angle_abs = cos(M_PI*origin2->angle_abs/180);
    origin2->sin_angle_abs = sin(M_PI*origin2->angle_abs/180);
    transform_point (origin1, &(origin2->ori), &(origin2->ori_abs));
    return 0;
}

int transform_angle (Origin* origin, double* a_rel, double* a_abs){
    set_angle (a_abs, *a_rel + origin->angle_abs);
    return 0;
}

int rigen_primitive_absolute_value(Origin* origin, Primitive* primitive){
    if (primitive==NULL)return -1;
    
    switch (primitive->type){
        case PRIM_LINE:
            transform_point(origin, &(primitive->line.start), &(primitive->line.start_abs));
            transform_point(origin, &(primitive->line.end), &(primitive->line.end_abs));
        break;
        case PRIM_CIRCLE:
            transform_point(origin, &(primitive->circle.center), &(primitive->circle.center_abs));
        break;
        case PRIM_ARC:
            transform_point(origin, &(primitive->arc.center), &(primitive->arc.center_abs));
            transform_point(origin, &(primitive->arc.p1), &(primitive->arc.p1_abs));
        break;
        case PRIM_RECTANGLE:
            transform_point(origin, &(primitive->rectangle.start), &(primitive->rectangle.start_abs));
            transform_point(origin, &(primitive->rectangle.end), &(primitive->rectangle.end_abs));
            transform_angle(origin, &(primitive->rectangle.angle), &(primitive->rectangle.angle_abs));
        break;
    default:
        break;
    }
    return 0;
}