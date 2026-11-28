/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Passes the image through unchanged",
    "CREDIT": "Zebra tests",
    "CATEGORIES": ["Utility"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" }
    ]
}*/

// Many ISF files carry this guard for GL ES hosts; desktop GL skips it.
#ifdef GL_ES
precision mediump float;
#endif

void main()
{
    gl_FragColor = IMG_THIS_PIXEL(inputImage);
}
