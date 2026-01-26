// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "vulkanbackground.h"
#include <QtCore/QRunnable>
#include <QtQuick/QQuickWindow>

#include <QVulkanInstance>
#include <QVulkanFunctions>
#include <QFile>
#include <QDateTime>
#include <QMatrix4x4>
#include <QVector3D>
#include <QImage>

class BackgroundRenderer : public QObject
{
    Q_OBJECT
public:
    ~BackgroundRenderer();

    void setT(qreal t) { m_t = t; }
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }

public slots:
    void frameStart();
    void mainPassRecordingStart();

private:
    enum Stage {
        VertexStage,
        FragmentStage
    };
    void prepareShader(Stage stage);
    void init(int framesInFlight);
    void loadTexture();
    void destroyTexture();
    void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    QSize m_viewportSize;
    qreal m_t = 0;
    QQuickWindow *m_window;

    QByteArray m_vert;
    QByteArray m_frag;

    bool m_initialized = false;
    VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_devFuncs = nullptr;
    QVulkanFunctions *m_funcs = nullptr;

    // Texture resources
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    } m_texture;

    // Buffer resources
    VkBuffer m_vbuf = VK_NULL_HANDLE;
    VkDeviceMemory m_vbufMem = VK_NULL_HANDLE;
    VkBuffer m_ibuf = VK_NULL_HANDLE;
    VkDeviceMemory m_ibufMem = VK_NULL_HANDLE;
    VkBuffer m_ubuf = VK_NULL_HANDLE;
    VkDeviceMemory m_ubufMem = VK_NULL_HANDLE;
    VkDeviceSize m_allocPerUbuf = 0;

    // Staging resources for texture
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;

    // Pipeline resources
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_resLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_ubufDescriptor = VK_NULL_HANDLE;

    uint32_t m_indexCount = 0;
};

VulkanBackground::VulkanBackground()
{
    connect(this, &QQuickItem::windowChanged, this, &VulkanBackground::handleWindowChanged);
}

void VulkanBackground::setT(qreal t)
{
    if (t == m_t)
        return;
    m_t = t;
    emit tChanged();
    if (window())
        window()->update();
}

void VulkanBackground::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &VulkanBackground::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &VulkanBackground::cleanup, Qt::DirectConnection);
    }
}

void VulkanBackground::cleanup()
{
    delete m_renderer;
    m_renderer = nullptr;
}

class BackgroundCleanupJob : public QRunnable
{
public:
    BackgroundCleanupJob(BackgroundRenderer *renderer) : m_renderer(renderer) { }
    void run() override { delete m_renderer; }
private:
    BackgroundRenderer *m_renderer;
};

void VulkanBackground::releaseResources()
{
    window()->scheduleRenderJob(new BackgroundCleanupJob(m_renderer), QQuickWindow::BeforeSynchronizingStage);
    m_renderer = nullptr;
}

BackgroundRenderer::~BackgroundRenderer()
{
    qDebug("background cleanup");
    if (!m_devFuncs)
        return;

    destroyTexture();

    m_devFuncs->vkDestroyPipeline(m_dev, m_pipeline, nullptr);
    m_devFuncs->vkDestroyPipelineLayout(m_dev, m_pipelineLayout, nullptr);
    m_devFuncs->vkDestroyDescriptorSetLayout(m_dev, m_resLayout, nullptr);

    m_devFuncs->vkDestroyDescriptorPool(m_dev, m_descriptorPool, nullptr);
    m_devFuncs->vkDestroyPipelineCache(m_dev, m_pipelineCache, nullptr);

    m_devFuncs->vkDestroyBuffer(m_dev, m_vbuf, nullptr);
    m_devFuncs->vkFreeMemory(m_dev, m_vbufMem, nullptr);

    m_devFuncs->vkDestroyBuffer(m_dev, m_ibuf, nullptr);
    m_devFuncs->vkFreeMemory(m_dev, m_ibufMem, nullptr);

    m_devFuncs->vkDestroyBuffer(m_dev, m_ubuf, nullptr);
    m_devFuncs->vkFreeMemory(m_dev, m_ubufMem, nullptr);

    qDebug("background released");
}

void BackgroundRenderer::destroyTexture()
{
    if (m_texture.sampler != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroySampler(m_dev, m_texture.sampler, nullptr);
        m_texture.sampler = VK_NULL_HANDLE;
    }
    if (m_texture.view != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImageView(m_dev, m_texture.view, nullptr);
        m_texture.view = VK_NULL_HANDLE;
    }
    if (m_texture.image != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImage(m_dev, m_texture.image, nullptr);
        m_texture.image = VK_NULL_HANDLE;
    }
    if (m_texture.memory != VK_NULL_HANDLE) {
        m_devFuncs->vkFreeMemory(m_dev, m_texture.memory, nullptr);
        m_texture.memory = VK_NULL_HANDLE;
    }
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyBuffer(m_dev, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingMemory != VK_NULL_HANDLE) {
        m_devFuncs->vkFreeMemory(m_dev, m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }
}

void VulkanBackground::sync()
{
    if (!m_renderer) {
        m_renderer = new BackgroundRenderer;
        // Рисуем фон ДО куба
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &BackgroundRenderer::frameStart, Qt::DirectConnection);
        connect(window(), &QQuickWindow::beforeRenderPassRecording, m_renderer, &BackgroundRenderer::mainPassRecordingStart, Qt::DirectConnection);
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
    m_renderer->setT(m_t);
    m_renderer->setWindow(window());
}

void BackgroundRenderer::frameStart()
{
    QSGRendererInterface *rif = m_window->rendererInterface();
    Q_ASSERT(rif->graphicsApi() == QSGRendererInterface::Vulkan);

    if (m_vert.isEmpty())
        prepareShader(VertexStage);
    if (m_frag.isEmpty())
        prepareShader(FragmentStage);

    if (!m_initialized)
        init(m_window->graphicsStateInfo().framesInFlight);
}

// Вершины для фона (большой квадрат на расстоянии 300 метров)
struct BackgroundVertex {
    float pos[3];
    float texCoord[2];
};

// Вершины для фона (растягиваем по высоте, сохраняя перспективу)
static const BackgroundVertex backgroundVertices[] = {
    // Координаты в NDC (Normalized Device Coordinates)
    // z = 1.0 - рисуем фон максимально далеко
    {{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}},
    {{1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}},
    {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}}
};

static const uint16_t backgroundIndices[] = {
    0, 1, 2, 2, 3, 0
};

const int BACKGROUND_UBUF_SIZE = sizeof(float) * 16 * 3 + sizeof(float);

void BackgroundRenderer::mainPassRecordingStart()
{
    const QQuickWindow::GraphicsStateInfo &stateInfo(m_window->graphicsStateInfo());
    QSGRendererInterface *rif = m_window->rendererInterface();

    // Обновляем uniform buffer
    VkDeviceSize ubufOffset = stateInfo.currentFrameSlot * m_allocPerUbuf;
    void *p = nullptr;
    VkResult err = m_devFuncs->vkMapMemory(m_dev, m_ubufMem, ubufOffset, m_allocPerUbuf, 0, &p);
    if (err != VK_SUCCESS || !p)
        qFatal("Failed to map uniform buffer memory: %d", err);

    // Для фона используем упрощенную матрицу
    QMatrix4x4 model;
    model.setToIdentity();


    QMatrix4x4 view;
    view.setToIdentity(); // Статичная камера

    QMatrix4x4 proj;
    proj.setToIdentity(); // Ортографическая проекция для полного экрана

    // Копируем матрицы в uniform buffer
    float *data = static_cast<float*>(p);
    memcpy(data, model.constData(), 16 * sizeof(float));
    memcpy(data + 16, view.constData(), 16 * sizeof(float));
    memcpy(data + 32, proj.constData(), 16 * sizeof(float));
    data[48] = m_t; // Время для возможных анимаций

    m_devFuncs->vkUnmapMemory(m_dev, m_ubufMem);

    m_window->beginExternalCommands();

    VkCommandBuffer cb = *reinterpret_cast<VkCommandBuffer *>(
        rif->getResource(m_window, QSGRendererInterface::CommandListResource));
    Q_ASSERT(cb);

    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkDeviceSize vbufOffset = 0;
    m_devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &m_vbuf, &vbufOffset);
    m_devFuncs->vkCmdBindIndexBuffer(cb, m_ibuf, 0, VK_INDEX_TYPE_UINT16);

    uint32_t dynamicOffset = m_allocPerUbuf * stateInfo.currentFrameSlot;
    m_devFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                                        &m_ubufDescriptor, 1, &dynamicOffset);

    // Устанавливаем depth test чтобы фон был позади куба
    m_devFuncs->vkCmdSetDepthTestEnable(cb, VK_TRUE);
    m_devFuncs->vkCmdSetDepthCompareOp(cb, VK_COMPARE_OP_LESS_OR_EQUAL);
    m_devFuncs->vkCmdSetDepthWriteEnable(cb, VK_FALSE); // Не записываем в буфер глубины

    VkViewport vp = { 0, 0, float(m_viewportSize.width()), float(m_viewportSize.height()), 0.0f, 1.0f };
    m_devFuncs->vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor = { { 0, 0 }, { uint32_t(m_viewportSize.width()), uint32_t(m_viewportSize.height()) } };
    m_devFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

    m_devFuncs->vkCmdDrawIndexed(cb, m_indexCount, 1, 0, 0, 0);

    m_window->endExternalCommands();
}

void BackgroundRenderer::prepareShader(Stage stage)
{
    QString filename;
    if (stage == VertexStage) {
        filename = QLatin1String(":/background.vert.spv");
    } else {
        Q_ASSERT(stage == FragmentStage);
        filename = QLatin1String(":/background.frag.spv");
    }
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("Failed to read shader %s", qPrintable(filename));

    const QByteArray contents = f.readAll();

    if (stage == VertexStage) {
        m_vert = contents;
        Q_ASSERT(!m_vert.isEmpty());
    } else {
        m_frag = contents;
        Q_ASSERT(!m_frag.isEmpty());
    }
}

static inline VkDeviceSize aligned(VkDeviceSize v, VkDeviceSize byteAlign)
{
    return (v + byteAlign - 1) & ~(byteAlign - 1);
}

void BackgroundRenderer::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    m_devFuncs->vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
        );
}

void BackgroundRenderer::copyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    m_devFuncs->vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void BackgroundRenderer::loadTexture()
{
    // Загружаем текстуру фона (JPG)
    QImage image(":/textures/background.jpg");
    if (image.isNull()) {
        // Если текстура не загружена, создаем градиентную текстуру
        image = QImage(1024, 1024, QImage::Format_RGBA8888);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QColor color;
                float fx = x / float(image.width());
                float fy = y / float(image.height());

                // Создаем градиент от синего к черному
                int r = int(30 * (1.0f - fx));
                int g = int(60 * (1.0f - fy));
                int b = int(120 * (0.5f + 0.5f * sin(fx * 3.14f)));

                color = QColor(r, g, b);
                image.setPixelColor(x, y, color);
            }
        }
    }

    image = image.convertToFormat(QImage::Format_RGBA8888);
    m_texture.width = image.width();
    m_texture.height = image.height();
    VkDeviceSize imageSize = m_texture.width * m_texture.height * 4;

    // Создаем staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_stagingBuffer);
    if (err != VK_SUCCESS)
        qFatal("Failed to create staging buffer: %d", err);

    VkMemoryRequirements memRequirements;
    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_stagingBuffer, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    m_funcs->vkGetPhysicalDeviceMemoryProperties(m_physDev, &memProperties);

    uint32_t memoryTypeIndex = uint32_t(-1);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == uint32_t(-1))
        qFatal("Failed to find suitable memory type for staging buffer");

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_stagingMemory);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate staging memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(m_dev, m_stagingBuffer, m_stagingMemory, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind staging buffer memory: %d", err);

    // Копируем данные изображения в staging buffer
    void* data;
    err = m_devFuncs->vkMapMemory(m_dev, m_stagingMemory, 0, imageSize, 0, &data);
    if (err != VK_SUCCESS)
        qFatal("Failed to map staging memory: %d", err);

    memcpy(data, image.constBits(), static_cast<size_t>(imageSize));
    m_devFuncs->vkUnmapMemory(m_dev, m_stagingMemory);

    // Создаем изображение
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_texture.width;
    imageInfo.extent.height = m_texture.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    err = m_devFuncs->vkCreateImage(m_dev, &imageInfo, nullptr, &m_texture.image);
    if (err != VK_SUCCESS)
        qFatal("Failed to create image: %d", err);

    m_devFuncs->vkGetImageMemoryRequirements(m_dev, m_texture.image, &memRequirements);

    memoryTypeIndex = uint32_t(-1);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == uint32_t(-1))
        qFatal("Failed to find suitable memory type for image");

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_texture.memory);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate image memory: %d", err);

    err = m_devFuncs->vkBindImageMemory(m_dev, m_texture.image, m_texture.memory, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind image memory: %d", err);

    // Копируем данные из staging buffer в изображение
    QSGRendererInterface *rif = m_window->rendererInterface();
    VkCommandBuffer commandBuffer = *reinterpret_cast<VkCommandBuffer *>(
        rif->getResource(m_window, QSGRendererInterface::CommandListResource));

    transitionImageLayout(commandBuffer, m_texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(commandBuffer, m_stagingBuffer, m_texture.image, m_texture.width, m_texture.height);
    transitionImageLayout(commandBuffer, m_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Создаем image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    err = m_devFuncs->vkCreateImageView(m_dev, &viewInfo, nullptr, &m_texture.view);
    if (err != VK_SUCCESS)
        qFatal("Failed to create texture image view: %d", err);

    // Создаем sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    err = m_devFuncs->vkCreateSampler(m_dev, &samplerInfo, nullptr, &m_texture.sampler);
    if (err != VK_SUCCESS)
        qFatal("Failed to create texture sampler: %d", err);
}

void BackgroundRenderer::init(int framesInFlight)
{
    Q_ASSERT(framesInFlight <= 3);
    m_initialized = true;

    QSGRendererInterface *rif = m_window->rendererInterface();
    QVulkanInstance *inst = reinterpret_cast<QVulkanInstance *>(
        rif->getResource(m_window, QSGRendererInterface::VulkanInstanceResource));
    Q_ASSERT(inst && inst->isValid());

    m_physDev = *reinterpret_cast<VkPhysicalDevice *>(rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
    m_dev = *reinterpret_cast<VkDevice *>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
    Q_ASSERT(m_physDev && m_dev);

    m_devFuncs = inst->deviceFunctions(m_dev);
    m_funcs = inst->functions();
    Q_ASSERT(m_devFuncs && m_funcs);

    VkRenderPass rp = *reinterpret_cast<VkRenderPass *>(
        rif->getResource(m_window, QSGRendererInterface::RenderPassResource));
    Q_ASSERT(rp);

    // Загружаем текстуру
    loadTexture();

    // Vertex buffer
    VkPhysicalDeviceProperties physDevProps;
    m_funcs->vkGetPhysicalDeviceProperties(m_physDev, &physDevProps);

    VkPhysicalDeviceMemoryProperties physDevMemProps;
    m_funcs->vkGetPhysicalDeviceMemoryProperties(m_physDev, &physDevMemProps);

    VkBufferCreateInfo bufferInfo;
    memset(&bufferInfo, 0, sizeof(bufferInfo));
    bufferInfo.size = sizeof(backgroundVertices);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkResult err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_vbuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_vbuf, &memReq);
    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;

    uint32_t memTypeIndex = uint32_t(-1);
    VkMemoryPropertyFlags memPropFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (memReq.memoryTypeBits & (1 << i)) {
            if ((physDevMemProps.memoryTypes[i].propertyFlags & memPropFlags) == memPropFlags) {
                memTypeIndex = i;
                break;
            }
        }
    }
    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find device memory type for vertex buffer");

    allocInfo.memoryTypeIndex = memTypeIndex;
    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_vbufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate vertex buffer memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(m_dev, m_vbuf, m_vbufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);

    void *p;
    err = m_devFuncs->vkMapMemory(m_dev, m_vbufMem, 0, bufferInfo.size, 0, &p);
    if (err != VK_SUCCESS)
        qFatal("Failed to map vertex buffer memory: %d", err);
    memcpy(p, backgroundVertices, bufferInfo.size);
    m_devFuncs->vkUnmapMemory(m_dev, m_vbufMem);

    // Index buffer
    bufferInfo.size = sizeof(backgroundIndices);
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_ibuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create index buffer: %d", err);

    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_ibuf, &memReq);
    allocInfo.allocationSize = memReq.size;
    memTypeIndex = uint32_t(-1);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (memReq.memoryTypeBits & (1 << i)) {
            if ((physDevMemProps.memoryTypes[i].propertyFlags & memPropFlags) == memPropFlags) {
                memTypeIndex = i;
                break;
            }
        }
    }
    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find device memory type for index buffer");

    allocInfo.memoryTypeIndex = memTypeIndex;
    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_ibufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate index buffer memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(m_dev, m_ibuf, m_ibufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind index buffer memory: %d", err);

    err = m_devFuncs->vkMapMemory(m_dev, m_ibufMem, 0, bufferInfo.size, 0, &p);
    if (err != VK_SUCCESS)
        qFatal("Failed to map index buffer memory: %d", err);
    memcpy(p, backgroundIndices, bufferInfo.size);
    m_devFuncs->vkUnmapMemory(m_dev, m_ibufMem);

    m_indexCount = sizeof(backgroundIndices) / sizeof(uint16_t);

    // Uniform buffer
    const VkDeviceSize ubufAlign = physDevProps.limits.minUniformBufferOffsetAlignment;
    m_allocPerUbuf = aligned(BACKGROUND_UBUF_SIZE, ubufAlign);
    bufferInfo.size = m_allocPerUbuf * framesInFlight;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_ubuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create uniform buffer: %d", err);

    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_ubuf, &memReq);
    allocInfo.allocationSize = memReq.size;
    memTypeIndex = uint32_t(-1);
    for (uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i) {
        if (memReq.memoryTypeBits & (1 << i)) {
            if ((physDevMemProps.memoryTypes[i].propertyFlags & memPropFlags) == memPropFlags) {
                memTypeIndex = i;
                break;
            }
        }
    }
    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find device memory type for uniform buffer");

    allocInfo.memoryTypeIndex = memTypeIndex;
    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_ubufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate uniform buffer memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(m_dev, m_ubuf, m_ubufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind uniform buffer memory: %d", err);

    // Pipeline layout
    VkDescriptorSetLayoutBinding layoutBinding[2];
    layoutBinding[0].binding = 0;
    layoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutBinding[0].descriptorCount = 1;
    layoutBinding[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBinding[0].pImmutableSamplers = nullptr;

    layoutBinding[1].binding = 1;
    layoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding[1].descriptorCount = 1;
    layoutBinding[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descLayoutInfo;
    memset(&descLayoutInfo, 0, sizeof(descLayoutInfo));
    descLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descLayoutInfo.bindingCount = 2;
    descLayoutInfo.pBindings = layoutBinding;
    err = m_devFuncs->vkCreateDescriptorSetLayout(m_dev, &descLayoutInfo, nullptr, &m_resLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor set layout: %d", err);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_resLayout;
    err = m_devFuncs->vkCreatePipelineLayout(m_dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    // Descriptor pool
    VkDescriptorPoolSize descPoolSizes[2];
    descPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descPoolSizes[0].descriptorCount = 1;
    descPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descPoolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo descPoolInfo;
    memset(&descPoolInfo, 0, sizeof(descPoolInfo));
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = 1;
    descPoolInfo.poolSizeCount = 2;
    descPoolInfo.pPoolSizes = descPoolSizes;
    err = m_devFuncs->vkCreateDescriptorPool(m_dev, &descPoolInfo, nullptr, &m_descriptorPool);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor pool: %d", err);

    // Descriptor set
    VkDescriptorSetAllocateInfo descSetAllocInfo;
    memset(&descSetAllocInfo, 0, sizeof(descSetAllocInfo));
    descSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descSetAllocInfo.descriptorPool = m_descriptorPool;
    descSetAllocInfo.descriptorSetCount = 1;
    descSetAllocInfo.pSetLayouts = &m_resLayout;
    err = m_devFuncs->vkAllocateDescriptorSets(m_dev, &descSetAllocInfo, &m_ubufDescriptor);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate descriptor set: %d", err);

    VkDescriptorBufferInfo bufferInfoDesc;
    bufferInfoDesc.buffer = m_ubuf;
    bufferInfoDesc.offset = 0;
    bufferInfoDesc.range = BACKGROUND_UBUF_SIZE;

    VkDescriptorImageInfo imageInfo;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_texture.view;
    imageInfo.sampler = m_texture.sampler;

    VkWriteDescriptorSet writeDescSet[2];
    memset(writeDescSet, 0, sizeof(writeDescSet));

    writeDescSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescSet[0].dstSet = m_ubufDescriptor;
    writeDescSet[0].dstBinding = 0;
    writeDescSet[0].descriptorCount = 1;
    writeDescSet[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writeDescSet[0].pBufferInfo = &bufferInfoDesc;

    writeDescSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescSet[1].dstSet = m_ubufDescriptor;
    writeDescSet[1].dstBinding = 1;
    writeDescSet[1].descriptorCount = 1;
    writeDescSet[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescSet[1].pImageInfo = &imageInfo;

    m_devFuncs->vkUpdateDescriptorSets(m_dev, 2, writeDescSet, 0, nullptr);

    // Pipeline cache
    VkPipelineCacheCreateInfo pipelineCacheInfo;
    memset(&pipelineCacheInfo, 0, sizeof(pipelineCacheInfo));
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    err = m_devFuncs->vkCreatePipelineCache(m_dev, &pipelineCacheInfo, nullptr, &m_pipelineCache);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline cache: %d", err);

    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo;
    memset(&pipelineInfo, 0, sizeof(pipelineInfo));
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shaderStages[2];
    memset(shaderStages, 0, sizeof(shaderStages));
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    VkShaderModule vertModule;
    VkShaderModuleCreateInfo shaderInfo;
    memset(&shaderInfo, 0, sizeof(shaderInfo));
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = m_vert.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t *>(m_vert.constData());
    err = m_devFuncs->vkCreateShaderModule(m_dev, &shaderInfo, nullptr, &vertModule);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex shader module: %d", err);
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkShaderModule fragModule;
    shaderInfo.codeSize = m_frag.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t *>(m_frag.constData());
    err = m_devFuncs->vkCreateShaderModule(m_dev, &shaderInfo, nullptr, &fragModule);
    if (err != VK_SUCCESS)
        qFatal("Failed to create fragment shader module: %d", err);
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    VkVertexInputBindingDescription vertexBindingDesc;
    vertexBindingDesc.binding = 0;
    vertexBindingDesc.stride = sizeof(BackgroundVertex);
    vertexBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexAttrDesc[2];
    vertexAttrDesc[0].location = 0;
    vertexAttrDesc[0].binding = 0;
    vertexAttrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttrDesc[0].offset = offsetof(BackgroundVertex, pos);
    vertexAttrDesc[1].location = 1;
    vertexAttrDesc[1].binding = 0;
    vertexAttrDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttrDesc[1].offset = offsetof(BackgroundVertex, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    memset(&vertexInputInfo, 0, sizeof(vertexInputInfo));
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;
    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.pInputAssemblyState = &ia;

    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    pipelineInfo.pViewportState = &vp;

    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // Не отсекаем грани
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rs;

    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.pMultisampleState = &ms;

    // Настройка теста глубины для фона
    VkPipelineDepthStencilStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE; // Не записываем в буфер глубины
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // Проходим тест если дальше
    pipelineInfo.pDepthStencilState = &ds;

    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    memset(&colorBlendAttachment, 0, sizeof(colorBlendAttachment));
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &colorBlendAttachment;
    pipelineInfo.pColorBlendState = &cb;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn;
    memset(&dyn, 0, sizeof(dyn));
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = rp;

    err = m_devFuncs->vkCreateGraphicsPipelines(m_dev, m_pipelineCache, 1, &pipelineInfo, nullptr, &m_pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);

    m_devFuncs->vkDestroyShaderModule(m_dev, vertModule, nullptr);
    m_devFuncs->vkDestroyShaderModule(m_dev, fragModule, nullptr);

    qDebug("background initialized");
}

#include "vulkanbackground.moc"
