/****************************************************************************************************************************************************
* Copyright (c) 2016 Freescale Semiconductor, Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright notice,
*      this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above copyright notice,
*      this list of conditions and the following disclaimer in the documentation
*      and/or other materials provided with the distribution.
*
*    * Neither the name of the Freescale Semiconductor, Inc. nor the names of
*      its contributors may be used to endorse or promote products derived from
*      this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
****************************************************************************************************************************************************/

#include <FslGraphics/Texture/Texture.hpp>
#include <FslGraphics/Texture/TextureBlobBuilder.hpp>
#include <FslGraphics/Texture/TextureTypeUtil.hpp>
#include <FslGraphics/Exceptions.hpp>
#include <FslGraphics/PixelFormatUtil.hpp>
//#include <FslGraphics/RawTextureUtil.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace Fsl
{
  namespace
  {
    //! @brief Returns the total texel count
    uint32_t ValidateBlobs(const std::vector<uint8_t>& srcContent, const std::vector<BlobRecord>& srcBlobs,
                           const Extent3D& extent, const PixelFormat pixelFormat, const TextureInfo& textureInfo)
    {
      const bool isCompressed = PixelFormatUtil::IsCompressed(pixelFormat);

      const std::size_t contentByteSize = srcContent.size();

      uint32_t totalTexelCount = 0;
      Extent3D currentExtent = extent;
      for (uint32_t level = 0; level < textureInfo.Levels; ++level)
      {
        if (level > 0 && (extent.Width == 0 || extent.Height == 0 || extent.Depth == 0))
          throw std::invalid_argument("The number of levels and the start extend causes zero sized extents at some levels");

        const uint32_t currentTexelCount = (currentExtent.Width * currentExtent.Height * currentExtent.Depth);
        for (uint32_t layer = 0; layer < textureInfo.Layers; ++layer)
        {
          for (uint32_t face = 0; face < textureInfo.Faces; ++face)
          {
            const std::size_t index = textureInfo.GetBlockIndex(level, face, layer);
            auto blob = srcBlobs[index];
            // two checks bypasses any overflow issues
            if (blob.Size > contentByteSize)
              throw std::invalid_argument("Content blob is outside the content buffer");
            if (blob.Offset >(contentByteSize - blob.Size))
              throw std::invalid_argument("Content blob is outside the content buffer");

            if (!isCompressed)
            {
              const auto stride = static_cast<std::size_t>(PixelFormatUtil::CalcMinimumStride(currentExtent.Width, pixelFormat));
              const auto expectedSize = currentExtent.Height * stride;
              if (expectedSize != blob.Size)
                throw std::invalid_argument("Content of the given pixelFormat and extent does not fit the blob size, invalid blob");
            }
            totalTexelCount += currentTexelCount;
          }
        }
        currentExtent.Width = std::max(currentExtent.Width >> 1, 1u);
        currentExtent.Height = std::max(currentExtent.Height >> 1, 1u);
        currentExtent.Depth = std::max(currentExtent.Depth >> 1, 1u);
      }
      return totalTexelCount;
    }
  }

  // move assignment operator
  Texture& Texture::operator=(Texture&& other)
  {
    if (this != &other)
    {
      // Free existing resources then transfer the content of other to this one and fill other with default values
      ResetNoThrow();

      // Claim ownership here
      m_content = std::move(other.m_content);
      m_blobs = std::move(other.m_blobs);
      m_extent = other.m_extent;
      m_pixelFormat = other.m_pixelFormat;
      m_textureType = other.m_textureType;
      m_textureInfo = other.m_textureInfo;
      m_totalTexels = other.m_totalTexels;
      m_bitmapOrigin = other.m_bitmapOrigin;
      m_isLocked = other.m_isLocked;

      // Remove the data from other
      other.m_extent = Extent3D();
      other.m_pixelFormat = PixelFormat::Undefined;
      other.m_textureType = TextureType::Undefined;
      other.m_textureInfo = TextureInfo();
      other.m_totalTexels = 0;
      other.m_bitmapOrigin = BitmapOrigin::Undefined;
      other.m_isLocked = false;
    }
    return *this;
  }


  // Transfer ownership from other to this
  Texture::Texture(Texture&& other)
    : m_content(std::move(other.m_content))
    , m_blobs(std::move(other.m_blobs))
    , m_extent(other.m_extent)
    , m_pixelFormat(other.m_pixelFormat)
    , m_textureType(other.m_textureType)
    , m_textureInfo(other.m_textureInfo)
    , m_totalTexels(other.m_totalTexels)
    , m_bitmapOrigin(other.m_bitmapOrigin)
    , m_isLocked(other.m_isLocked)
  {
    // Remove the data from other
    other.m_extent = Extent3D();
    other.m_pixelFormat = PixelFormat::Undefined;
    other.m_textureType = TextureType::Undefined;
    other.m_textureInfo = TextureInfo();
    other.m_totalTexels = 0;
    other.m_bitmapOrigin = BitmapOrigin::Undefined;
    other.m_isLocked = false;
  }


  Texture::Texture()
    : m_content()
    , m_blobs()
    , m_extent()
    , m_pixelFormat(PixelFormat::Undefined)
    , m_textureType(TextureType::Undefined)
    , m_textureInfo()
    , m_totalTexels(0)
    , m_bitmapOrigin(BitmapOrigin::Undefined)
    , m_isLocked(false)
  {
  }


  Texture::Texture(const TextureBlobBuilder& builder)
    : Texture()
  {
    Reset(builder);
  }


  Texture::Texture(const void*const pContent, const std::size_t contentByteSize, const TextureBlobBuilder& builder)
    : Texture()
  {
    Reset(pContent, contentByteSize, builder);
  }


  Texture::Texture(const void*const pContent, const std::size_t contentByteSize, TextureBlobBuilder&& builder)
    : Texture()
  {
    Reset(pContent, contentByteSize, std::move(builder));
  }


  Texture::Texture(std::vector<uint8_t>&& content, TextureBlobBuilder&& builder)
    : Texture()
  {
    Reset(std::move(content), std::move(builder));
  }


  Texture::~Texture()
  {
    FSLLOG_WARNING_IF(m_isLocked, "Destroying a locked texture, the content being accessed will no longer be available");
  }


  void Texture::Reset()
  {
    // Reset() should not throw, but this warrants a program stop since its a critical error
    if (m_isLocked)
      throw UsageErrorException("Can not reset a locked texture, that would invalidate the content being accessed");

    ResetNoThrow();
  }


  void Texture::Reset(const TextureBlobBuilder& builder)
  {
      if (m_isLocked)
        throw UsageErrorException("Can not reset a locked texture, that would invalidate the content being accessed");

    if ( ! builder.IsValid() )
      throw std::invalid_argument("build can not be invalid");

    DoReset(nullptr, 0, builder);
  }


  void Texture::Reset(const void*const pContent, const std::size_t contentByteSize, const TextureBlobBuilder& builder)
  {
    if (m_isLocked)
      throw UsageErrorException("Can not reset a locked texture, that would invalidate the content being accessed");

    if (pContent == nullptr)
      throw NotSupportedException("The pContent buffer can not be null");
    if (!builder.IsValid())
      throw std::invalid_argument("build can not be invalid");
    if (contentByteSize != builder.GetContentSize())
      throw NotSupportedException("the builder content size did not match the buffer size");

    DoReset(pContent, contentByteSize, builder);
  }


  void Texture::Reset(const void*const pContent, const std::size_t contentByteSize, TextureBlobBuilder&& builder)
  {
    if (m_isLocked)
      throw UsageErrorException("Can not reset a locked texture, that would invalidate the content being accessed");

    // TODO: claim the vector from the builder to prevent unnecessary allocations
    Reset(pContent, contentByteSize, builder);
  }


  void Texture::Reset(std::vector<uint8_t>&& content, TextureBlobBuilder&& builder)
  {
    if (m_isLocked)
      throw UsageErrorException("Can not reset a locked texture, that would invalidate the content being accessed");

    if (!builder.IsValid())
      throw std::invalid_argument("build can not be invalid");
    if (content.size() != builder.GetContentSize())
      throw NotSupportedException("the builder content size did not match the buffer size");

    DoReset(std::move(content), std::move(builder));
  }


  BitmapOrigin Texture::GetBitmapOrigin() const
  {
    return m_bitmapOrigin;
  }


  uint32_t Texture::GetFaces() const
  {
    return m_textureInfo.Faces;
  }


  Extent3D Texture::GetExtent(const std::size_t level) const
  {
    if (!IsValid())
      throw UsageErrorException("GetExtent called on invalid object");
    if (level >= m_textureInfo.Levels)
      throw std::invalid_argument("argument out of bounds");

    return level == 0 ? m_extent : Extent3D(std::max(m_extent.Width >> level, 1u), std::max(m_extent.Height >> level, 1u), std::max(m_extent.Depth >> level, 1u));
  }


  uint32_t Texture::GetLayers() const
  {
    return m_textureInfo.Layers;
  }


  uint32_t Texture::GetLevels() const
  {
    return m_textureInfo.Levels;
  }


  PixelFormat Texture::GetPixelFormat() const
  {
    return m_pixelFormat;
  }


  PixelFormatLayout Texture::GetPixelFormatLayout() const
  {
    return PixelFormatUtil::GetPixelFormatLayout(m_pixelFormat);
  }


  TextureType Texture::GetTextureType() const
  {
    return m_textureType;
  }


  uint32_t Texture::GetTotalTexelCount() const
  {
    return m_totalTexels;
  }


  std::size_t Texture::GetBlobCount() const
  {
    return m_blobs.size();
  }


  BlobRecord Texture::GetBlob(const std::size_t index) const
  {
    if (!IsValid())
      throw UsageErrorException("GetBlobs() called on invalid Texture");
    if (index >= m_blobs.size())
      throw UsageErrorException("index out of bounds");
    return m_blobs[index];
  }


  BlobRecord Texture::GetTextureBlob(const uint32_t level, const uint32_t face, const uint32_t layer) const
  {
    if (level >= m_textureInfo.Levels || face >= m_textureInfo.Faces || layer >= m_textureInfo.Layers)
      throw std::invalid_argument("out of bounds");

    const std::size_t index = m_textureInfo.GetBlockIndex(level, face, layer);
    return m_blobs[index];
  }


  uint32_t Texture::GetByteSize() const
  {
    assert(m_content.size() <= std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(m_content.size());
  }


  void Texture::SwapCompatibleBlobs(const uint32_t level, const uint32_t face0, const uint32_t layer0, const uint32_t face1, const uint32_t layer1)
  {
    if (m_isLocked)
      throw UsageErrorException("Can not SwapCompatibleBlobs a locked texture, that would invalidate the content being accessed");

    if (level >= m_textureInfo.Levels || face0 >= m_textureInfo.Faces || layer0 >= m_textureInfo.Layers || face1 >= m_textureInfo.Faces || layer1 >= m_textureInfo.Layers)
      throw std::invalid_argument("out of bounds");

    const std::size_t index0 = m_textureInfo.GetBlockIndex(level, face0, layer0);
    const std::size_t index1 = m_textureInfo.GetBlockIndex(level, face1, layer1);
    if (index0 == index1)
      return;

    std::swap(m_blobs[index0], m_blobs[index1]);
  }


  void Texture::SetCompatiblePixelFormat(const PixelFormat compatiblePixelFormat, const bool allowBytePerPixelCompatible)
  {
    if (m_isLocked)
      throw UsageErrorException("The texture is already locked");

    if (allowBytePerPixelCompatible && PixelFormatUtil::GetBytesPerPixel(m_pixelFormat) != PixelFormatUtil::GetBytesPerPixel(compatiblePixelFormat))
      throw UsageErrorException("The supplied pixel format was not byte per pixel compatible");

    if (!allowBytePerPixelCompatible && PixelFormatUtil::GetPixelFormatLayout(m_pixelFormat) != PixelFormatUtil::GetPixelFormatLayout(compatiblePixelFormat))
      throw UsageErrorException("The supplied pixel format was not compatible");

    m_pixelFormat = compatiblePixelFormat;
  }


  void Texture::DoReset(const void*const pContent, const std::size_t contentByteSize, const TextureBlobBuilder& builder)
  {
    // If any of these fire the builder did not keep its contract or
    // we forgot to validate a input parameter somewhere
    assert(!m_isLocked);
    assert(builder.IsValid());
    assert(builder.GetTextureType() != TextureType::Undefined);
    assert(builder.GetPixelFormat() != PixelFormat::Undefined);
    assert(builder.GetBitmapOrigin() != BitmapOrigin::Undefined);
    assert(builder.GetLevels() >= 1);
    assert(builder.GetFaces() == TextureTypeUtil::GetFaceCount(builder.GetTextureType()));
    assert(builder.GetLayers() >= 1);
    assert(builder.GetBlobCount() == (builder.GetLevels() * builder.GetFaces() * builder.GetLayers()));
    assert(pContent == nullptr || contentByteSize == builder.GetContentSize());

    if (IsValid())
      Reset();

    try
    {
      const auto blobCount = builder.GetBlobCount();
      m_content.resize(builder.GetContentSize());
      m_blobs.resize(blobCount);

      for (std::size_t i = 0; i < blobCount; ++i)
        m_blobs[i] = builder.GetBlobByIndex(i);

      const uint32_t totalTexels = ValidateBlobs(m_content, m_blobs, builder.GetExtent(), builder.GetPixelFormat(), builder.GetTextureInfo());
      if (totalTexels != builder.GetTotalTexelCount())
        throw std::invalid_argument("The builder does not contain the expected amount of texels in the blobs");
    }
    catch (const std::exception&)
    {
      m_content.clear();
      m_blobs.clear();
      throw;
    }

    // Just copy the data old school style
    if (pContent != nullptr && contentByteSize > 0)
      std::memcpy(m_content.data(), pContent, contentByteSize);
    m_extent = builder.GetExtent();
    m_pixelFormat = builder.GetPixelFormat();
    m_textureType = builder.GetTextureType();
    m_textureInfo = builder.GetTextureInfo();
    m_totalTexels = builder.GetTotalTexelCount();
    m_bitmapOrigin = builder.GetBitmapOrigin();
  }


  void Texture::DoReset(std::vector<uint8_t>&& content, TextureBlobBuilder&& builder)
  {
    // If any of these fire the builder did not keep its contract or
    // we forgot to validate a input parameter somewhere
    assert(!m_isLocked);
    assert(builder.IsValid());
    assert(builder.GetTextureType() != TextureType::Undefined);
    assert(builder.GetPixelFormat() != PixelFormat::Undefined);
    assert(builder.GetBitmapOrigin() != BitmapOrigin::Undefined);
    assert(builder.GetLevels() >= 1);
    assert(builder.GetFaces() == TextureTypeUtil::GetFaceCount(builder.GetTextureType()));
    assert(builder.GetLayers() >= 1);
    assert(builder.GetBlobCount() == (builder.GetLevels() * builder.GetFaces() * builder.GetLayers()));
    assert(content.size() == builder.GetContentSize());

    if (IsValid())
      Reset();

    try
    {
      const auto blobCount = builder.GetBlobCount();
      m_content = std::move(content);
      m_blobs.resize(blobCount);

      for (std::size_t i = 0; i < blobCount; ++i)
        m_blobs[i] = builder.GetBlobByIndex(i);

      const uint32_t totalTexels = ValidateBlobs(m_content, m_blobs, builder.GetExtent(), builder.GetPixelFormat(), builder.GetTextureInfo());
      if (totalTexels != builder.GetTotalTexelCount())
        throw std::invalid_argument("The builder does not contain the expected amount of texels in the blobs");
    }
    catch (const std::exception&)
    {
      m_content.clear();
      m_blobs.clear();
      throw;
    }

    // We don't copy the content here size we 'moved' the source into this object
    m_extent = builder.GetExtent();
    m_pixelFormat = builder.GetPixelFormat();
    m_textureType = builder.GetTextureType();
    m_textureInfo = builder.GetTextureInfo();
    m_totalTexels = builder.GetTotalTexelCount();
    m_bitmapOrigin = builder.GetBitmapOrigin();
  }


  RawTexture Texture::Lock() const
  {
    if (m_isLocked)
      throw UsageErrorException("The texture is already locked");
    m_isLocked = true;
    return RawTexture(m_textureType, m_content.data(), m_content.size(), m_blobs.data(), m_blobs.size(), m_extent, m_pixelFormat, m_textureInfo, m_bitmapOrigin);
  }


  RawTextureEx Texture::LockEx()
  {
    if (m_isLocked)
      throw UsageErrorException("The texture is already locked");

    m_isLocked = true;
    return RawTextureEx(m_textureType, m_content.data(), m_content.size(), m_blobs.data(), m_blobs.size(), m_extent, m_pixelFormat, m_textureInfo, m_bitmapOrigin);
  }


  void Texture::UnlockEx(const RawTextureEx& texture)
  {
    if (!m_isLocked)
      throw UsageErrorException("The texture is not locked");

    BitmapOrigin currentOrigin = texture.GetBitmapOrigin();
    if (currentOrigin != m_bitmapOrigin)
      m_bitmapOrigin = currentOrigin;

    m_isLocked = false;
  }


  void Texture::Unlock() const
  {
    if (!m_isLocked)
      throw UsageErrorException("The texture is not locked");
    m_isLocked = false;
  }


  void Texture::ResetNoThrow()
  {
    FSLLOG_WARNING_IF(m_isLocked, "Destroying a locked texture, the content being accessed will no longer be available");
    m_content.clear();
    m_blobs.clear();
    m_extent = Extent3D();
    m_pixelFormat = PixelFormat::Undefined;
    m_textureType = TextureType::Undefined;
    m_textureInfo = TextureInfo();
    m_totalTexels = 0;
    m_bitmapOrigin = BitmapOrigin::Undefined;
    m_isLocked = false;
  }

}
