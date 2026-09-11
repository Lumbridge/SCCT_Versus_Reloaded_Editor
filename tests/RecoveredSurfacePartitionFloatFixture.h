// Native material subdivisions with consecutive vertices identical in float32.
inline void VerifyOffsDFloatImportCase()
{
    const std::vector<Vec3> polygon={{1527.9998064225547,2264.0000000000009,-4288},{1315.9226074218759,2264.0000000000009,-4288},{1421.961181640625,2370.03857421875,-4288},{1421.9611820228711,2370.0385738365039,-4288}};
    std::vector<std::vector<Vec3>> pieces;
    std::string error;
    assert(SplitForVertexLimit(polygon,{0,0,-1},16,pieces,error));
    assert(pieces.size()==1 && pieces[0].size()==3);
}
inline void VerifyLegacyD1FloatImportCase()
{
    const std::vector<Vec3> polygon={{1503.8250976640361,-3384.07763671875,1.8189894035458565e-12},{1503.8250313483786,-3384.07763671875,1.8189894035458565e-12},{1503.8046451164228,-3256.3305457250863,1.8189894035458565e-12},{1503.8046875,-3256.0822753911907,1.8189894035458565e-12},{1503.8046875000755,-3256.0822758646732,1.8189894035458565e-12}};
    std::vector<std::vector<Vec3>> pieces;
    std::string error;
    assert(SplitForVertexLimit(polygon,{0,0,-1},16,pieces,error));
    assert(pieces.size()==1 && pieces[0].size()==3);
}
