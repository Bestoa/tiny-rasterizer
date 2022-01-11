#include <iostream>
#include <glm/glm.hpp>
#include "buffer.hpp"
#include "texture.hpp"

namespace TGRenderer
{
    unsigned int gCurrentID = 1;

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
        screen_v.y = mVY + (mH - 1) * (ndc_v.y / 2 + 0.5);
    }

    void TRBuffer::setBgColor(float r, float g, float b)
    {
        mBgColor3i[0] = glm::clamp(int(r * 255 + 0.5), 0, 255);
        mBgColor3i[1] = glm::clamp(int(g * 255 + 0.5), 0, 255);
        mBgColor3i[2] = glm::clamp(int(b * 255 + 0.5), 0, 255);

        mBgColor3f[0] = r;
        mBgColor3f[1] = g;
        mBgColor3f[2] = b;
    }

    void TRBuffer::clearColor()
    {
        for (size_t i = 0; i < mW * mH * BUFFER_CHANNEL; i += BUFFER_CHANNEL)
        {
            mData[i + 0] = mBgColor3i[0];
            mData[i + 1] = mBgColor3i[1];
            mData[i + 2] = mBgColor3i[2];
            if (BUFFER_CHANNEL == 4)
                mData[i + 3] = 255;
        }
    }

    void TRBuffer::clearDepth()
    {
        for (size_t i = 0; i < mW * mH; i++)
            mDepth[i] = 1.0f;
    }

    void TRBuffer::clearStencil()
    {
        for (size_t i = 0; i < mW * mH; i++)
            mStencil[i] = 0;
    }

    size_t TRBuffer::getStride()
    {
        return mW;
    }

    size_t TRBuffer::getOffset(int x, int y)
    {
        return y * mW + x;
    }

    void TRBuffer::drawPixel(int x, int y, float color[])
    {
        // flip Y here
        uint8_t *base = mData + ((mH - 1 - y) * mW + x) * BUFFER_CHANNEL;
        base[0] = uint8_t(color[0] * 255 + 0.5);
        base[1] = uint8_t(color[1] * 255 + 0.5);
        base[2] = uint8_t(color[2] * 255 + 0.5);
    }

    bool TRBuffer::depthTest(size_t offset, float depth)
    {
        /* projection matrix will inverse z-order. */
        if (mDepth[offset] < depth)
            return false;
        else
            mDepth[offset] = depth;
        return true;
    }

    void TRBuffer::stencilFunc(size_t offset)
    {
        /* simple stencil func */
        mStencil[offset]++;
    }

    bool TRBuffer::stencilTest(size_t offset)
    {
        return mStencil[offset] == 0;
    }

#if __NEED_BUFFER_LOCK__
    std::mutex & TRBuffer::getMutex(size_t offset)
    {
        return mMutex[offset >> MUTEX_PIXEL_SHIT];
    }
#endif

    void TRBuffer::setExtBuffer(void *addr)
    {
        if (mValid && !mAlloc)
            mData = reinterpret_cast<uint8_t *>(addr);
    }

    TRBuffer::TRBuffer(int w, int h, bool alloc)
    {
        mId = gCurrentID++;
        std::cout << "Create TRBuffer: " << w << "x" << h << " alloc = " << alloc << " ID = " << mId << std::endl;
        mAlloc = alloc;
        if (mAlloc)
            mData = new uint8_t[w * h * BUFFER_CHANNEL];
        if (mAlloc && mData == nullptr)
            return;
        mDepth = new float[w * h];
        mStencil = new uint8_t[w * h];
        if (mDepth == nullptr || mStencil == nullptr)
            goto error;
#if __NEED_BUFFER_LOCK__
        mMutex = new std::mutex[((w * h) >> MUTEX_PIXEL_SHIT) + 1];
        if (mMutex == nullptr)
            goto error;
#endif
        mW = mVW = w;
        mH = mVH = h;
        if (mAlloc)
            clearColor();
        clearDepth();
        clearStencil();

        mValid = true;

        return;
error:
        if (mData)
            delete mData;
        if (mDepth)
            delete mDepth;
        if (mStencil)
            delete mStencil;
    }

    TRBuffer::~TRBuffer()
    {
        std::cout << "Destory TRBuffer, ID = " << mId << std::endl;
        if (!mValid)
            return;
        if (mAlloc && mData)
            delete mData;
        if (mDepth)
            delete mDepth;
        if (mStencil)
            delete mStencil;
#if __NEED_BUFFER_LOCK__
        if (mMutex)
            delete [] mMutex;
#endif
    }

    bool TRBuffer::isValid()
    {
        return mValid;
    }

    TRTextureBuffer::TRTextureBuffer(int w, int h) : TRBuffer::TRBuffer(w, h, false)
    {
        if (!mValid)
            return;
        mTexture = new TRTexture(w, h);
        if (!mTexture || !mTexture->isValid())
            mValid = false;
    }

    TRTextureBuffer::~TRTextureBuffer()
    {
        if (mTexture)
            delete mTexture;
    }

    void TRTextureBuffer::clearColor()
    {
        float *base = mTexture->getBuffer();
        for (size_t i = 0; i < mW * mH * TEXTURE_CHANNEL; i += TEXTURE_CHANNEL)
        {
            base[i + 0] = mBgColor3f[0];
            base[i + 1] = mBgColor3f[1];
            base[i + 2] = mBgColor3f[2];
            if (TEXTURE_CHANNEL == 4)
                base[i + 3] = 1.0f;
        }
    }

    void TRTextureBuffer::drawPixel(int x, int y, float color[])
    {
        float *base = mTexture->getBuffer() + (y * mW + x) * TEXTURE_CHANNEL;
        base[0] = color[0];
        base[1] = color[1];
        base[2] = color[2];
    }

    TRTexture* TRTextureBuffer::getTexture()
    {
        return mTexture;
    }
}
