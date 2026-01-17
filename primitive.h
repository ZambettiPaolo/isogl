#ifndef PRIMITIVE_H
#define PRIMITIVE_H

#define MAX_PATTERN_LENGTH 9
#define LINE_TYPE_COUNT 15

// ISO 128 patterns: defined as multiples of line thickness
// According to ISO 128-2, patterns are ratios relative to thickness
typedef struct {
    const char* name;
    float pattern_ratios[MAX_PATTERN_LENGTH]; // Multiples of line thickness
    int patternLength;
} LinePattern;

typedef enum {
    LINE_TYPE_CONTINUOUS = 0,
    LINE_TYPE_DASHED,
    LINE_TYPE_DASHED_SPACED,
    LINE_TYPE_LONG_DASH_DOT,
    LINE_TYPE_LONG_DASH_DOUBLE_DOT,
    LINE_TYPE_LONG_DASH_TRIPLE_DOT,
    LINE_TYPE_DOTTED,
    LINE_TYPE_LONG_DASH_SHORT_DASH,
    LINE_TYPE_LONG_DASH_DOUBLE_SHORT_DASH,
    LINE_TYPE_DASH_DOT,
    LINE_TYPE_DASH_DOUBLE_DOT,
    LINE_TYPE_DOUBLE_DOT_DASH,
    LINE_TYPE_DOUBLE_DOT_DOUBLE_DASH,
    LINE_TYPE_TRIPLE_DOT_DASH,
    LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH
} LineTypeISO;



// ISO 128 patterns: values are multiples of line thickness
// According to standard: dash=12t, gap=3t, dot=0 (point), where t=line thickness
static const LinePattern iso128Patterns[LINE_TYPE_COUNT] = {
    [LINE_TYPE_CONTINUOUS] = {
        .name = "Continuous", 
        .pattern_ratios = {0}, 
        .patternLength = 0  // 0 = continuous
    },
    [LINE_TYPE_DASHED] = {
        .name = "Dashed", 
        .pattern_ratios = {12.0f, 3.0f},  // 12t dash, 3t gap
        .patternLength = 2
    },
    [LINE_TYPE_DASHED_SPACED] = {
        .name = "Dashed Spaced", 
        .pattern_ratios = {12.0f, 6.0f},  // 12t dash, 6t gap
        .patternLength = 2
    },
    [LINE_TYPE_LONG_DASH_DOT] = {
        .name = "Long Dash Dot", 
        .pattern_ratios = {20.0f, 3.0f, 2.0f, 3.0f},  // 20t dash, 3t gap, 2dot, 3t gap
        .patternLength = 4
    },
    [LINE_TYPE_LONG_DASH_DOUBLE_DOT] = {
        .name = "Long Dash Double Dot", 
        .pattern_ratios = {20.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f}, 
        .patternLength = 6
    },
    [LINE_TYPE_LONG_DASH_TRIPLE_DOT] = {
        .name = "Long Dash Triple Dot", 
        .pattern_ratios = {20.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f}, 
        .patternLength = 8
    },
    [LINE_TYPE_DOTTED] = {
        .name = "Dotted", 
        .pattern_ratios = {0.0f, 3.0f},  // dot, 3t gap
        .patternLength = 2
    },
    [LINE_TYPE_LONG_DASH_SHORT_DASH] = {
        .name = "Long Dash Short Dash", 
        .pattern_ratios = {20.0f, 3.0f, 6.0f, 3.0f},  // 20t dash, 3t gap, 6t dash, 3t gap
        .patternLength = 4
    },
    [LINE_TYPE_LONG_DASH_DOUBLE_SHORT_DASH] = {
        .name = "Long Dash Double Short Dash", 
        .pattern_ratios = {20.0f, 3.0f, 6.0f, 3.0f, 6.0f, 3.0f}, 
        .patternLength = 6
    },
    [LINE_TYPE_DASH_DOT] = {
        .name = "Dash Dot", 
        .pattern_ratios = {12.0f, 3.0f, 0.0f, 3.0f},  // 12t dash, 3t gap, dot, 3t gap
        .patternLength = 4
    },
    [LINE_TYPE_DASH_DOUBLE_DOT] = {
        .name = "Dash Double Dot", 
        .pattern_ratios = {12.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f}, 
        .patternLength = 6
    },
    [LINE_TYPE_DOUBLE_DOT_DASH] = {
        .name = "Double Dot Dash", 
        .pattern_ratios = {0.0f, 3.0f, 0.0f, 3.0f, 12.0f, 3.0f}, 
        .patternLength = 6
    },
    [LINE_TYPE_DOUBLE_DOT_DOUBLE_DASH] = {
        .name = "Double Dot Double Dash", 
        .pattern_ratios = {0.0f, 3.0f, 0.0f, 3.0f, 6.0f, 3.0f, 6.0f, 3.0f}, 
        .patternLength = 8
    },
    [LINE_TYPE_TRIPLE_DOT_DASH] = {
        .name = "Triple Dot Dash", 
        .pattern_ratios = {0.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f, 12.0f, 3.0f}, 
        .patternLength = 8
    },
    [LINE_TYPE_TRIPLE_DOT_DOUBLE_DASH] = {
        .name = "Triple Dot Double Dash", 
        .pattern_ratios = {0.0f, 3.0f, 0.0f, 3.0f, 0.0f, 3.0f, 6.0f, 3.0f, 6.0f}, 
        .patternLength = 9
    }
};

// ISO standard line weights enum
typedef enum {
    LINE_THICKNESS_0_18 = 0,    // 0.18mm - finest
    LINE_THICKNESS_0_25,        // 0.25mm - thin
    LINE_THICKNESS_0_35,        // 0.35mm - medium-thin
    LINE_THICKNESS_0_5,         // 0.5mm - medium
    LINE_THICKNESS_0_7,         // 0.7mm - medium-thick
    LINE_THICKNESS_1_0,         // 1.0mm - thick
    LINE_THICKNESS_1_4,         // 1.4mm - very thick
    LINE_THICKNESS_2_0,         // 2.0mm - thickest
    LINE_THICKNESS_COUNT
} LineWeight;

// ISO standard line weights lookup table
static const float line_weights[LINE_THICKNESS_COUNT] = {
    [LINE_THICKNESS_0_18] = 0.18f,
    [LINE_THICKNESS_0_25] = 0.25f,
    [LINE_THICKNESS_0_35] = 0.35f,
    [LINE_THICKNESS_0_5]  = 0.5f,
    [LINE_THICKNESS_0_7]  = 0.7f,
    [LINE_THICKNESS_1_0]  = 1.0f,
    [LINE_THICKNESS_1_4]  = 1.4f,
    [LINE_THICKNESS_2_0]  = 2.0f
};

//ISO standard text dimention enum
typedef enum {
    TEXT_HEIGHT_1_8,
    TEXT_HEIGHT_2_5,
    TEXT_HEIGHT_3_5,
    TEXT_HEIGHT_5,
    TEXT_HEIGHT_7,
    TEXT_HEIGHT_10,
    TEXT_HEIGHT_14,
    TEXT_HEIGHT_20,
    TEXT_HEIGHT_CONT
}TextHeight;

// ISO standard text dimention lookup table
static const float text_height[TEXT_HEIGHT_CONT] = {
    [TEXT_HEIGHT_1_8] = 0.18f,
    [TEXT_HEIGHT_2_5] = 0.25f,
    [TEXT_HEIGHT_3_5] = 0.35f,
    [TEXT_HEIGHT_5]  = 0.5f,
    [TEXT_HEIGHT_7]  = 0.7f,
    [TEXT_HEIGHT_10]  = 1.0f,
    [TEXT_HEIGHT_14]  = 1.4f,
    [TEXT_HEIGHT_20]  = 2.0f
};

typedef enum {
    LEFT_ALIGNEMENT,
    RIGTH_ALIGNEMENT,
    CENTERD
}TextAlignement;


typedef enum {
    PRIM_LINE,
    PRIM_BOX,
    PRIM_CIRCLE,
    PRIM_ARC,
    PRIM_RECTANGLE,
    PRIM_text
} PrimitiveType;

typedef struct {
    double x;
    double y;
} Point;

typedef struct {
    Point ori;
    double angle;
    Point ori_abs;
    double angle_abs;
    double cos_angle_abs;
    double sin_angle_abs;     
} Origin;

typedef struct {
    Point left_bottom;//absolute
    Point right_top;//absolute
}Box;

typedef struct {
    Point center;
    Point center_abs;
    double radius;
} Circle;

typedef struct {
    Point center;
    Point center_abs;
    Point p1;
    Point p1_abs;
    double angle;
} Arc;  

typedef struct {
    Point start;
    Point end;
    Point start_abs;
    Point end_abs;

} Line;

typedef struct {
    Point start;
    Point end;
    double angle;
    Point start_abs;
    Point end_abs;
    double angle_abs;
} Rectangle;

typedef struct {
    Point start;
    double angle;
    Point start_abs;
    double angle_abs;
    char* s;
    TextHeight t_height;
    TextAlignement t_alignement;
    
} Text;


typedef struct Primitive Primitive;
    struct Primitive {
    Primitive *next;
    PrimitiveType type;
	LineTypeISO pattern;
    LineWeight thickness;
    int visible;
    union {
        Box box;
        Line line;   
        Circle circle;
        Arc arc;
        Rectangle rectangle;
        Text text;
    };
};

int set_angle(double* angle, double new_angle);
int rigen_origin_abs(Origin* origin1, Origin* origin2);
int transform_point (Origin* origin, Point* p_rel, Point* p_abs);
int transform_angle (Origin* origin, double* a_rel, double* a_abs);
int rigen_primitive_absolute_value(Origin* origin, Primitive* primitive);





#endif // PRIMITIVE_H
