#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "tr.hpp"

// global value
TRBuffer *gRenderTarget = nullptr;
TRTexture *gTexture[TEXTURE_INDEX_MAX] = { nullptr };

glm::mat4 gMat4[MAT_INDEX_MAX] =
{
    glm::mat4(1.0f), // model mat
    glm::mat4(1.0f), // view mat
    // defult proj mat should swap z-order
    glm::mat4({1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}), // proj mat
};

glm::mat3 gMat3[MAT_INDEX_MAX] =
{
    glm::mat3(1.0f), // normal mat
};

bool gEnableLighting = false;
float gAmbientStrength = 0.1;
float gSpecularStrength = 0.2;
int gShininess = 32;
glm::vec3 gLightColor = glm::vec3(1.0f, 1.0f, 1.0f);
glm::vec3 gLightPosition = glm::vec3(1.0f, 1.0f, 1.0f);

size_t gThreadNum = 4;

// internal function
void __texture_coord_repeat__(glm::vec2 &coord)
{
    coord.x = glm::fract(coord.x);
    coord.y = glm::fract(coord.y);
}

void __texture_coord_clamp_edge__(glm::vec2 &coord)
{
    coord.x = glm::clamp(coord.x, 0.0f, 1.0f);
    coord.y = glm::clamp(coord.y, 0.0f, 1.0f);
}

void textureCoordWrap(glm::vec2 &coord)
{
    if (coord.x < 0 || coord.x > 1 || coord.y < 0 || coord.y > 1)
       __texture_coord_repeat__(coord);
}

void TRMeshData::computeTangent()
{
    for (size_t i = 0; i < vertices.size(); i += 3)
    {
        // Edges of the triangle : postion delta
        glm::vec3 deltaPos1 = vertices[i+1] - vertices[i];
        glm::vec3 deltaPos2 = vertices[i+2] - vertices[i];

        // UV delta
        glm::vec2 deltaUV1 = uvs[i+1]-uvs[i];
        glm::vec2 deltaUV2 = uvs[i+2]-uvs[i];

        float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);

        glm::vec3 T = (deltaPos1 * deltaUV2.y   - deltaPos2 * deltaUV1.y) * r;
        tangents.push_back(T);
    }
}

void TRMeshData::fillDemoColor()
{
    if (colors.size() != 0)
        return;

    glm::vec3 demoColor[3] = { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) };

    for (size_t i = 0; i < vertices.size(); i++)
        colors.push_back(demoColor[i % 3]);
}

void TRBuffer::setViewport(int x, int y, int w, int h)
{
    mVX = x;
    mVY = y;
    mVW = w;
    mVH = h;
}

void TRBuffer::viewport(glm::vec2 &screen_v, glm::vec4 &ndc_v)
{
    screen_v.x = mVX + (mW - 1) * (ndc_v.x / 2 + 0.5);
    // inverse y here
    screen_v.y = mVY + (mH - 1) - (mH - 1) * (ndc_v.y / 2 + 0.5);
}

void TRBuffer::setBGColor(float r, float g, float b)
{
    mBGColor[0] = glm::clamp(int(r * 255 + 0.5), 0, 255);
    mBGColor[1] = glm::clamp(int(g * 255 + 0.5), 0, 255);
    mBGColor[2] = glm::clamp(int(b * 255 + 0.5), 0, 255);
}

void TRBuffer::clearColor()
{
    for (uint32_t i = 0; i < mH; i++)
    {
        uint8_t *base = &mData[i * mStride];
        for (uint32_t j = 0; j < mW; j++)
        {
            base[j * CHANNEL + 0] = mBGColor[0];
            base[j * CHANNEL + 1] = mBGColor[1];
            base[j * CHANNEL + 2] = mBGColor[2];
        }
    }
}

void TRBuffer::clearDepth()
{
    for (size_t i = 0; i < mW * mH; i++)
        mDepth[i] = 1.0f;
}

bool TRBuffer::depthTest(int x, int y, float depth)
{
    size_t depth_offset = y * mW + x;
    /* projection matrix will inverse z-order. */
    if (mDepth[depth_offset] < depth)
        return false;
    else
        mDepth[depth_offset] = depth;
    return true;
}

void TRBuffer::setExtBuffer(void *addr)
{
    if (mValid && mExtBuffer)
        mData = reinterpret_cast<uint8_t *>(addr);
}

TRBuffer* TRBuffer::create(int w, int h, bool ext)
{
    TRBuffer *buffer = new TRBuffer(w, h, ext);
    if (buffer && buffer->mValid)
        return buffer;

    if (buffer)
        delete buffer;
    return nullptr;
}

TRBuffer::TRBuffer(int w, int h, bool ext)
{
    std::cout << "Create TRBuffer: " << w << "x" << h << " ext = " << ext << std::endl;
    mExtBuffer = ext;
    if (!ext)
    {
        mData = new uint8_t[w * h * CHANNEL];
        if (!mData)
            return;
    }
    mDepth = new float[w * h];
    if (!mDepth)
    {
        if (mData)
            delete mData;
        return;
    }
    mW = mVW = w;
    mH = mVH = h;
    mStride = w * CHANNEL;
    if (!ext)
        clearColor();
    clearDepth();

    mValid = true;
}

TRBuffer::~TRBuffer()
{
    std::cout << "Destory TRBuffer.\n";
    if (!mValid)
        return;
    if (!mExtBuffer && mData)
        delete mData;
    if (mDepth)
        delete mDepth;
}

static inline void __compute_premultiply_mat__()
{
    gMat4[MAT4_MODELVIEW] =  gMat4[MAT4_VIEW] * gMat4[MAT4_MODEL];
    gMat4[MAT4_MVP] = gMat4[MAT4_PROJ] * gMat4[MAT4_MODELVIEW];
    gMat3[MAT3_NORMAL] = glm::transpose(glm::inverse(gMat4[MAT4_MODELVIEW]));
}

void __plot__(TRBuffer *buffer, int x, int y)
{
    uint8_t *base = &buffer->mData[y * buffer->mStride + x * CHANNEL];
    base[0] = base[1] = base[2] = 255;
}

void __draw_line__(TRBuffer *buffer, float x0, float y0, float x1, float y1)
{
    // Bresenham's line algorithm
    bool steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep)
    {
        std::swap(x0, y0);
        std::swap(x1, y1);
    }

    if (x0 > x1)
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    int deltax = int(x1 - x0);
    int deltay = int(abs(y1 - y0));
    float error = 0;
    float deltaerr = (float)deltay / (float)deltax;
    int ystep;
    int y = y0;
    if (y0 < y1)
        ystep = 1;
    else
        ystep = -1;
    for (int x = int(x0 + 0.5); x < x1; x++)
    {
        if (steep)
            __plot__(buffer, y, x);
        else
            __plot__(buffer, x, y);
        error += deltaerr;
        if (error >= 0.5)
        {
            y += ystep;
            error -= 1.0;
        }
    }
}

// tr api
void WireframeProgram::loadVertexData(TRMeshData &mesh, VSDataBase vsdata[3], size_t index)
{
    for (int i = 0; i < 3; i++)
    {
        vsdata[i].mVertex = mesh.vertices[i + index];
    }
}

void WireframeProgram::vertex(VSDataBase &vsdata, glm::vec4 &clipV)
{
    (void)vsdata;
    (void)clipV;
}

bool WireframeProgram::geometry(VSDataBase vsdata[3], FSDataBase &fsdata)
{
    (void)fsdata;
    glm::vec4 clip_v[3];
    glm::vec4 ndc_v[3];
    glm::vec2 screen_v[3];

    for (int i = 0; i < 3; i++)
    {
        clip_v[i] = gMat4[MAT4_MVP] * glm::vec4(vsdata[i].mVertex, 1.0f);
        ndc_v[i] = clip_v[i] / clip_v[i].w;
        mBuffer->viewport(screen_v[i], ndc_v[i]);
    }
    __draw_line__(mBuffer, screen_v[0].x, screen_v[0].y, screen_v[1].x, screen_v[1].y);
    __draw_line__(mBuffer, screen_v[1].x, screen_v[1].y, screen_v[2].x, screen_v[2].y);
    __draw_line__(mBuffer, screen_v[2].x, screen_v[2].y, screen_v[0].x, screen_v[0].y);

    return false;
}

bool WireframeProgram::fragment(FSDataBase &fsdata, float color[3])
{
    (void)fsdata;
    (void)color;
    return false;
}

void ColorProgram::loadVertexData(TRMeshData &mesh, VSDataBase vsdata[3], size_t index)
{
    for (int i = 0; i < 3; i++)
    {
        vsdata[i].mVertex = mesh.vertices[i + index];
        vsdata[i].mColor = mesh.colors[i + index];
    }
}

void ColorProgram::vertex(VSDataBase &vsdata, glm::vec4 &clipV)
{
    clipV = gMat4[MAT4_MVP] * glm::vec4(vsdata.mVertex, 1.0f);
}

bool ColorProgram::geometry(VSDataBase vsdata[3], FSDataBase &fsdata)
{
    fsdata.mColor[0] = vsdata[0].mColor;
    fsdata.mColor[1] = vsdata[1].mColor - vsdata[0].mColor;
    fsdata.mColor[2] = vsdata[2].mColor - vsdata[0].mColor;
    return true;
}

bool ColorProgram::fragment(FSDataBase &fsdata, float color[3])
{
    glm::vec3 C = interpFast(fsdata.mColor);
    color[0] = C[0];
    color[1] = C[1];
    color[2] = C[2];
    return true;
}

void TextureMapProgram::loadVertexData(TRMeshData &mesh, VSDataBase vsdata[3], size_t index)
{
    for (int i = 0; i < 3; i++)
    {
        vsdata[i].mVertex = mesh.vertices[i + index];
        vsdata[i].mTexCoord = mesh.uvs[i + index];
    }
}

void TextureMapProgram::vertex(VSDataBase &vsdata, glm::vec4 &clipV)
{
    clipV = gMat4[MAT4_MVP] * glm::vec4(vsdata.mVertex, 1.0f);
}

bool TextureMapProgram::geometry(VSDataBase vsdata[3], FSDataBase &fsdata)
{
    fsdata.mTexCoord[0] = vsdata[0].mTexCoord;
    fsdata.mTexCoord[1] = vsdata[1].mTexCoord - vsdata[0].mTexCoord;
    fsdata.mTexCoord[2] = vsdata[2].mTexCoord - vsdata[0].mTexCoord;
    return true;
}

bool TextureMapProgram::fragment(FSDataBase &fsdata, float color[3])
{
    glm::vec2 texCoord = interpFast(fsdata.mTexCoord);
    textureCoordWrap(texCoord);

    float *c = gTexture[TEXTURE_DIFFUSE]->getColor(texCoord.x, texCoord.y);

    color[0] = c[0];
    color[1] = c[1];
    color[2] = c[2];
    return true;
}

void ColorPhongProgram::loadVertexData(TRMeshData &mesh, PhongVSData vsdata[3], size_t index)
{
    glm::vec3 viewLightPostion = gMat4[MAT4_VIEW] * glm::vec4(gLightPosition, 1.0f);

    for (int i = 0; i < 3; i++)
    {
        vsdata[i].mVertex = mesh.vertices[i + index];
        vsdata[i].mNormal = mesh.normals[i + index];
        vsdata[i].mColor = mesh.colors[i + index];
        vsdata[i].mLightPosition = viewLightPostion;
    }
}

void ColorPhongProgram::vertex(PhongVSData &vsdata, glm::vec4 &clipV)
{
    clipV = gMat4[MAT4_MVP] * glm::vec4(vsdata.mVertex, 1.0f);
    vsdata.mViewFragPosition = gMat4[MAT4_MODELVIEW] * glm::vec4(vsdata.mVertex, 1.0f);
    vsdata.mNormal = gMat3[MAT3_NORMAL] * vsdata.mNormal;
}

bool ColorPhongProgram::geometry(PhongVSData vsdata[3], PhongFSData &fsdata)
{
    fsdata.mViewFragPosition[0] = vsdata[0].mViewFragPosition;
    fsdata.mViewFragPosition[1] = vsdata[1].mViewFragPosition - vsdata[0].mViewFragPosition;
    fsdata.mViewFragPosition[2] = vsdata[2].mViewFragPosition - vsdata[0].mViewFragPosition;

    fsdata.mNormal[0] = vsdata[0].mNormal;
    fsdata.mNormal[1] = vsdata[1].mNormal - vsdata[0].mNormal;
    fsdata.mNormal[2] = vsdata[2].mNormal - vsdata[0].mNormal;

    fsdata.mColor[0] = vsdata[0].mColor;
    fsdata.mColor[1] = vsdata[1].mColor - vsdata[0].mColor;
    fsdata.mColor[2] = vsdata[2].mColor - vsdata[0].mColor;

    fsdata.mLightPosition = vsdata[0].mLightPosition;
    return true;
}

bool ColorPhongProgram::fragment(PhongFSData &fsdata, float color[3])
{
    glm::vec3 viewFragPosition = interpFast(fsdata.mViewFragPosition);
    glm::vec3 normal = interpFast(fsdata.mNormal);
    glm::vec3 baseColor = interpFast(fsdata.mColor);

    normal = glm::normalize(normal);
    // from fragment to light
    glm::vec3 lightDirection = glm::normalize(fsdata.mLightPosition - viewFragPosition);

    float diff = glm::max(dot(normal, lightDirection), 0.0f);

    // in camera space, eys always in (0.0, 0.0, 0.0), from fragment to eye
    glm::vec3 eyeDirection = glm::normalize(-viewFragPosition);
#if __BLINN_PHONG__
    glm::vec3 halfwayDirection = glm::normalize(lightDirection + eyeDirection);
    float spec = glm::pow(glm::max(dot(normal, halfwayDirection), 0.0f), gShininess * 2);
#else
    glm::vec3 reflectDirection = glm::reflect(-lightDirection, normal);
    float spec = glm::pow(glm::max(dot(eyeDirection, reflectDirection), 0.0f), gShininess);
#endif
    glm::vec3 result = (gAmbientStrength + diff * gLightColor) * baseColor + spec * gSpecularStrength * gLightColor;

    color[0] = glm::min(result[0], 1.f);
    color[1] = glm::min(result[1], 1.f);
    color[2] = glm::min(result[2], 1.f);

    return true;
}

void TextureMapPhongProgram::loadVertexData(TRMeshData &mesh, PhongVSData vsdata[3], size_t index)
{
    glm::vec3 viewLightPostion = gMat4[MAT4_VIEW] * glm::vec4(gLightPosition, 1.0f);

    for (int i = 0; i < 3; i++)
    {
        vsdata[i].mVertex = mesh.vertices[i + index];
        vsdata[i].mTexCoord = mesh.uvs[i + index];
        vsdata[i].mNormal = mesh.normals[i + index];
        vsdata[i].mLightPosition = viewLightPostion;
        vsdata[i].mTangent = mesh.tangents[i / 3];
    }
}

void TextureMapPhongProgram::vertex(PhongVSData &vsdata, glm::vec4 &clipV)
{
    clipV = gMat4[MAT4_MVP] * glm::vec4(vsdata.mVertex, 1.0f);
    vsdata.mViewFragPosition = gMat4[MAT4_MODELVIEW] * glm::vec4(vsdata.mVertex, 1.0f);
    vsdata.mNormal = gMat3[MAT3_NORMAL] * vsdata.mNormal;
}

bool TextureMapPhongProgram::geometry(PhongVSData vsdata[3], PhongFSData &fsdata)
{
    fsdata.mTexCoord[0] = vsdata[0].mTexCoord;
    fsdata.mTexCoord[1] = vsdata[1].mTexCoord - vsdata[0].mTexCoord;
    fsdata.mTexCoord[2] = vsdata[2].mTexCoord - vsdata[0].mTexCoord;

    fsdata.mViewFragPosition[0] = vsdata[0].mViewFragPosition;
    fsdata.mViewFragPosition[1] = vsdata[1].mViewFragPosition - vsdata[0].mViewFragPosition;
    fsdata.mViewFragPosition[2] = vsdata[2].mViewFragPosition - vsdata[0].mViewFragPosition;

    fsdata.mNormal[0] = vsdata[0].mNormal;
    fsdata.mNormal[1] = vsdata[1].mNormal - vsdata[0].mNormal;
    fsdata.mNormal[2] = vsdata[2].mNormal - vsdata[0].mNormal;

    fsdata.mLightPosition = vsdata[0].mLightPosition;
    fsdata.mTangent = glm::normalize(gMat3[MAT3_NORMAL] * vsdata[0].mTangent);
    return true;
}

bool TextureMapPhongProgram::fragment(PhongFSData &fsdata, float color[3])
{
    glm::vec3 viewFragPosition = interpFast(fsdata.mViewFragPosition);
    glm::vec3 normal = interpFast(fsdata.mNormal);
    glm::vec2 texCoord = interpFast(fsdata.mTexCoord);
    textureCoordWrap(texCoord);

    glm::vec3 baseColor = glm::make_vec3(gTexture[TEXTURE_DIFFUSE]->getColor(texCoord.x, texCoord.y));

    if (gTexture[TEXTURE_NORMAL] != nullptr)
    {
        // Gram-Schmidt orthogonalize
        glm::vec3 T = fsdata.mTangent;
        T = glm::normalize(T - dot(T, normal) * normal);
        glm::vec3 B = glm::cross(normal, T);
        glm::mat3 TBN = glm::mat3(T, B, normal);
        normal = glm::make_vec3(gTexture[TEXTURE_NORMAL]->getColor(texCoord.x, texCoord.y)) * 2.0f - 1.0f;
        normal = TBN * normal;
    }
    normal = glm::normalize(normal);
    // from fragment to light
    glm::vec3 lightDirection = glm::normalize(fsdata.mLightPosition - viewFragPosition);

    float diff = glm::max(dot(normal, lightDirection), 0.0f);

    // in camera space, eys always in (0.0, 0.0, 0.0), from fragment to eye
    glm::vec3 eyeDirection = glm::normalize(-viewFragPosition);
#if __BLINN_PHONG__
    glm::vec3 halfwayDirection = glm::normalize(lightDirection + eyeDirection);
    float spec = glm::pow(glm::max(dot(normal, halfwayDirection), 0.0f), gShininess * 2);
#else
    glm::vec3 reflectDirection = glm::reflect(-lightDirection, normal);
    float spec = glm::pow(glm::max(dot(eyeDirection, reflectDirection), 0.0f), gShininess);
#endif
    glm::vec3 specular = spec * gLightColor;

    if (gTexture[TEXTURE_SPECULAR] != nullptr)
        specular *= glm::make_vec3(gTexture[TEXTURE_SPECULAR]->getColor(texCoord.x, texCoord.y));
    else
        specular *= gSpecularStrength;

    glm::vec3 result = (gAmbientStrength + diff * gLightColor) * baseColor + specular;

    if (gTexture[TEXTURE_GLOW] != nullptr)
        result += glm::make_vec3(gTexture[TEXTURE_GLOW]->getColor(texCoord.x, texCoord.y));

    color[0] = glm::min(result[0], 1.f);
    color[1] = glm::min(result[1], 1.f);
    color[2] = glm::min(result[2], 1.f);

    return true;
}

template <class TRVSData, class TRFSData>
void TRProgramBase<TRVSData, TRFSData>::drawTriangle(TRMeshData &mesh, size_t index)
{
    glm::vec4 clipV[3];

    loadVertexData(mesh, mVSData, index * 3);
    for (size_t i = 0; i < 3; i++)
        vertex(mVSData[i], clipV[i]);

    if(geometry(mVSData, mFSData))
        rasterization(clipV);
}

template <class TRVSData, class TRFSData>
void TRProgramBase<TRVSData, TRFSData>::drawTrianglesInstanced(TRBuffer *buffer, TRMeshData &mesh, size_t index, size_t num)
{
    size_t i = 0, j = 0;
    size_t trianglesNum = mesh.vertices.size() / 3;

    mBuffer = buffer;

    for (i = index, j = 0; i < trianglesNum && j < num; i++, j++)
        drawTriangle(mesh, i);
}

template <class TRVSData, class TRFSData>
void TRProgramBase<TRVSData, TRFSData>::rasterization(glm::vec4 clip_v[3])
{
    glm::vec4 ndc_v[3];
    glm::vec2 screen_v[3];

    for (int i = 0; i < 3; i++)
    {
        ndc_v[i] = clip_v[i] / clip_v[i].w;
        mBuffer->viewport(screen_v[i], ndc_v[i]);
    }

    float area = edge(screen_v[0], screen_v[1], screen_v[2]);
    if (area <= 0)
        return;

    int xStart = glm::max(0.0f, glm::min(glm::min(screen_v[0].x, screen_v[1].x), screen_v[2].x)) + 0.5;
    int yStart = glm::max(0.0f, glm::min(glm::min(screen_v[0].y, screen_v[1].y), screen_v[2].y)) + 0.5;
    int xEnd = glm::min(float(mBuffer->mW - 1), glm::max(glm::max(screen_v[0].x, screen_v[1].x), screen_v[2].x)) + 1.5;
    int yEnd = glm::min(float(mBuffer->mH - 1), glm::max(glm::max(screen_v[0].y, screen_v[1].y), screen_v[2].y)) + 1.5;

    float Z[3] = { ndc_v[0].z, ndc_v[1].z - ndc_v[0].z, ndc_v[2].z - ndc_v[0].z };

    for (int y = yStart; y < yEnd; y++) {
        int offset_base = y * mBuffer->mW;
        for (int x = xStart; x < xEnd; x++) {
            glm::vec2 v(x, y);
            float w0 = edge(screen_v[1], screen_v[2], v);
            float w1 = edge(screen_v[2], screen_v[0], v);
            float w2 = edge(screen_v[0], screen_v[1], v);
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;

            /* Barycentric coordinate */
            w0 /= area;
            w1 /= area;
            w2 /= area;

            /* Perspective-Correct */
            w0 /= clip_v[0].w;
            w1 /= clip_v[1].w;
            w2 /= clip_v[2].w;

            float areaPC =  1 / (w0 + w1 + w2);
            mUPC = w1 * areaPC;
            mVPC = w2 * areaPC;

            float depth = interpFast(Z);

            /* z in ndc of opengl should between 0.0f to 1.0f */
            if (depth < 0.0f)
                continue;

            int offset = offset_base + x;
            /* easy-z */
            /* Do not use mutex here to speed up */
            if (!mBuffer->depthTest(x, y, depth))
                continue;

            float color[3];
            if (!fragment(mFSData, color))
                continue;

            uint8_t *addr = &mBuffer->mData[offset * CHANNEL];
            /* depth test */
            {
                std::lock_guard<std::mutex> lck(mBuffer->mDepthMutex);
                if (!mBuffer->depthTest(x, y, depth))
                    continue;

                addr[0] = uint8_t(color[0] * 255 + 0.5);
                addr[1] = uint8_t(color[1] * 255 + 0.5);
                addr[2] = uint8_t(color[2] * 255 + 0.5);
            }
        }
    }
}

template<class Program>
void trTrianglesInstanced(TRMeshData &mesh, size_t index, size_t num)
{
    Program prog;
    prog.drawTrianglesInstanced(gRenderTarget, mesh, index, num);
}

template<class Program>
void trTrianglesMT(TRMeshData &mesh)
{
    if (gThreadNum > 1)
    {
        std::vector<std::thread> thread_pool;
        size_t index_step = mesh.vertices.size() / 3 / gThreadNum + 1;

        for (size_t i = 0; i < gThreadNum; i++)
            thread_pool.push_back(std::thread(trTrianglesInstanced<Program>, std::ref(mesh), i * index_step, index_step));

        for (auto &th : thread_pool)
            if (th.joinable())
                th.join();
    } else {
        trTrianglesInstanced<Program>(mesh, 0, mesh.vertices.size() / 3);
    }
}

void trTriangles(TRMeshData &mesh, TRDrawMode mode)
{
    __compute_premultiply_mat__();

    switch (mode)
    {
        case DRAW_WITH_TEXTURE:
            if (mesh.tangents.size() == 0)
                mesh.computeTangent();
            if (gEnableLighting)
                trTrianglesMT<TextureMapPhongProgram>(mesh);
            else
                trTrianglesMT<TextureMapProgram>(mesh);
            break;
        case DRAW_WITH_DEMO_COLOR:
            mesh.fillDemoColor();
            if (gEnableLighting)
                trTrianglesMT<ColorPhongProgram>(mesh);
            else
                trTrianglesMT<ColorProgram>(mesh);
            break;
        case DRAW_WITH_COLOR:
            if (gEnableLighting)
                trTrianglesMT<ColorPhongProgram>(mesh);
            else
                trTrianglesMT<ColorProgram>(mesh);
            break;
        case DRAW_WIREFRAME:
            trTrianglesMT<WireframeProgram>(mesh);
            break;
    }
}

void trSetRenderThreadNum(size_t num)
{
    gThreadNum = num;
}

void trEnableLighting(bool enable)
{
    gEnableLighting = enable;
}

void trSetAmbientStrength(float v)
{
    gAmbientStrength = v;
}

void trSetSpecularStrength(float v)
{
    gSpecularStrength = v;
}

void trSetShininess(int v)
{
    gShininess = v;
}

void trSetLightColor3f(float r, float g, float b)
{
    gLightColor = glm::vec3(r, g, b);
}

void trSetLightPosition3f(float x, float y, float z)
{
    gLightPosition = glm::vec3(x, y, z);
}

void trViewport(int x, int y, int w, int h)
{
    gRenderTarget->setViewport(x, y, w, h);
}

void trMakeCurrent(TRBuffer *buffer)
{
    gRenderTarget = buffer;
}

void trBindTexture(TRTexture *texture, int type)
{
    if (type < TEXTURE_INDEX_MAX)
        gTexture[type] = texture;
    else
        std::cout << "Unknow texture type!\n";
}

void trClear()
{
    gRenderTarget->clearColor();
    gRenderTarget->clearDepth();
}

void trClearColor3f(float r, float g, float b)
{
    gRenderTarget->setBGColor(r, g, b);
}

void trSetModelMat(glm::mat4 mat)
{
    gMat4[MAT4_MODEL] = mat;
}

void trSetViewMat(glm::mat4 mat)
{
    gMat4[MAT4_VIEW] = mat;
}

void trSetProjMat(glm::mat4 mat)
{
    gMat4[MAT4_PROJ] = mat;
}

TRBuffer * trCreateRenderTarget(int w, int h)
{
    return TRBuffer::create(w, h);
}

void trSetCurrentRenderTarget(TRBuffer *buffer)
{
    gRenderTarget = buffer;
}
