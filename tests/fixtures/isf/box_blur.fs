/*{
    "ISFVSN": "2",
    "DESCRIPTION": "A 3 x 3 box blur",
    "CREDIT": "Govtech Visualizer tests",
    "CATEGORIES": ["Blur"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" }
    ]
}*/

const float kOffsets[3] = float[3](-1.0, 0.0, 1.0);

void main()
{
    // gl_FragCoord is in render pixels; scale it to the image's own pixels.
    vec2 pixel = gl_FragCoord.xy * IMG_SIZE(inputImage) / RENDERSIZE;
    vec4 sum = vec4(0.0);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            sum += IMG_PIXEL(inputImage, pixel + vec2(kOffsets[x], kOffsets[y]));
    gl_FragColor = sum / 9.0;
}
