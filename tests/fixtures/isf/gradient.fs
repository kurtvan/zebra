/*{
    "ISFVSN": "2",
    "DESCRIPTION": "A gradient (x in red, y in green), tinted, with a blue spot and a white flash",
    "CREDIT": "Zebra tests",
    "CATEGORIES": ["Generator"],
    "INPUTS": [
        { "NAME": "tint", "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] },
        { "NAME": "center", "TYPE": "point2D", "DEFAULT": [0.0, 0.0], "MIN": [0.0, 0.0], "MAX": [16.0, 16.0] },
        { "NAME": "mode", "TYPE": "long", "VALUES": [0, 1], "LABELS": ["Gradient", "Grey"], "DEFAULT": 0 },
        { "NAME": "spot", "TYPE": "bool", "DEFAULT": false },
        { "NAME": "flash", "TYPE": "event" }
    ]
}*/

void main()
{
    vec4 color = mode == 1 ? vec4(0.5, 0.5, 0.5, 1.0) : vec4(isf_FragNormCoord, 0.0, 1.0);
    // point2D inputs are in render pixels.
    if (spot && distance(gl_FragCoord.xy, center) < 1.0)
        color.b = 1.0;
    if (flash)
        color = vec4(1.0);
    gl_FragColor = color * tint;
}
