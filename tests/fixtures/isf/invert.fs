/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Inverts the colours, mixed in by amount",
    "CREDIT": "Zebra tests",
    "CATEGORIES": ["Color Effect"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "amount", "LABEL": "Amount", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0 }
    ]
}*/

// The old ISF v1 spellings: vv_FragNormCoord and texture2D.
void main()
{
    vec4 color = texture2D(inputImage, vv_FragNormCoord);
    gl_FragColor = vec4(mix(color.rgb, 1.0 - color.rgb, amount), color.a);
}
