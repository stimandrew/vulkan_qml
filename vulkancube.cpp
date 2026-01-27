// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "vulkancube.h"
#include <QtCore/QRunnable>
#include <QtQuick/QQuickWindow>

#include <QVulkanInstance>
#include <QVulkanFunctions>
#include <QFile>
#include <QDateTime>
#include <QMatrix4x4>
#include <QVector3D>
#include <QImage>
#include <QFileInfo>
#include "model_loader.h"

class CubeRenderer : public QObject
{
    Q_OBJECT
public:
    ~CubeRenderer();

    void setT(qreal t) { m_t = t; }
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }
    void setCubePosition(qreal x, qreal y, qreal z) {
        m_cubePositionX = x;
        m_cubePositionY = y;
        m_cubePositionZ = z;
    }
    void loadDefaultCube();
    void setUseCustomModel(bool use) { m_useCustomModel = use; }
signals:
    void needLoadDefaultCube();
public slots:
    void frameStart();
    void mainPassRecordingStart();
    void loadCustomModel(const QString& filePath);

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
    void recreateBuffers();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties,
                            const VkPhysicalDeviceMemoryProperties& memProperties) {
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return uint32_t(-1);
    }

    VkShaderModule createShaderModule(const QByteArray& code) {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.constData());

        VkShaderModule shaderModule;
        VkResult err = m_devFuncs->vkCreateShaderModule(m_dev, &createInfo, nullptr, &shaderModule);
        if (err != VK_SUCCESS) {
            qFatal("Failed to create shader module: %d", err);
        }
        return shaderModule;
    }

    QSize m_viewportSize;
    qreal m_t = 0;
    qreal m_cubePositionX = 0;
    qreal m_cubePositionY = 0;
    qreal m_cubePositionZ = -5;
    QQuickWindow *m_window;

    bool m_useCustomModel = false;
    QVector<VertexData> m_customVertices;
    QVector<uint32_t> m_customIndices;
    ModelLoader m_modelLoader;

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

VulkanCube::VulkanCube()
{
    connect(this, &QQuickItem::windowChanged, this, &VulkanCube::handleWindowChanged);
}

void VulkanCube::setModelPath(const QUrl& path)
{
    if (m_modelPath == path)
        return;
    m_modelPath = path;
    emit modelPathChanged();

    if (window())
        window()->update();
}

void VulkanCube::setUseCustomModel(bool use)
{
    if (m_useCustomModel == use)
        return;
    m_useCustomModel = use;

    // Если переключаемся на использование куба, загружаем куб по умолчанию
    if (!use && m_renderer) {
        // Загружаем куб по умолчанию через сигнал
        QMetaObject::invokeMethod(this, [this]() {
                if (m_renderer) {
                    m_renderer->loadDefaultCube();
                }
            }, Qt::QueuedConnection);
    }

    emit useCustomModelChanged();

    if (window())
        window()->update();
}

void VulkanCube::loadModel(const QUrl& fileUrl)
{
    QString filePath;
    if (fileUrl.isLocalFile()) {
        filePath = fileUrl.toLocalFile();
    } else {
        qWarning() << "Model file must be a local file";
        return;
    }

    setModelPath(fileUrl);
    setUseCustomModel(true);

    // Загружаем модель
    if (m_renderer) {
        m_renderer->loadCustomModel(filePath);
    }

    emit modelLoaded();

    if (window())
        window()->update();
}

void VulkanCube::setT(qreal t)
{
    if (t == m_t)
        return;
    m_t = t;
    emit tChanged();
    if (window())
        window()->update();
}

void VulkanCube::setCubePositionX(qreal x)
{
    if (qFuzzyCompare(m_cubePositionX, x))
        return;
    m_cubePositionX = x;
    emit cubePositionXChanged();
    if (window())
        window()->update();
}

void VulkanCube::setCubePositionY(qreal y)
{
    if (qFuzzyCompare(m_cubePositionY, y))
        return;
    m_cubePositionY = y;
    emit cubePositionYChanged();
    if (window())
        window()->update();
}

void VulkanCube::setCubePositionZ(qreal z)
{
    if (qFuzzyCompare(m_cubePositionZ, z))
        return;
    m_cubePositionZ = z;
    emit cubePositionZChanged();
    if (window())
        window()->update();
}

void VulkanCube::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &VulkanCube::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &VulkanCube::cleanup, Qt::DirectConnection);
        win->setColor(Qt::lightGray);
    }
}

void VulkanCube::cleanup()
{
    delete m_renderer;
    m_renderer = nullptr;
}

class CubeCleanupJob : public QRunnable
{
public:
    CubeCleanupJob(CubeRenderer *renderer) : m_renderer(renderer) { }
    void run() override { delete m_renderer; }
private:
    CubeRenderer *m_renderer;
};

void VulkanCube::releaseResources()
{
    window()->scheduleRenderJob(new CubeCleanupJob(m_renderer), QQuickWindow::BeforeSynchronizingStage);
    m_renderer = nullptr;
}

CubeRenderer::~CubeRenderer()
{

    qDebug("cube cleanup");
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

    qDebug("cube released");
}

void CubeRenderer::loadDefaultCube()
{
    // Загружаем куб по умолчанию через ModelLoader
    m_customVertices.clear();
    m_customIndices.clear();

    if (!m_modelLoader.loadBuiltInCube(m_customVertices, m_customIndices)) {
        qWarning() << "Failed to load built-in cube";
        return;
    }

    recreateBuffers();

    m_indexCount = m_customIndices.size();
    m_useCustomModel = false;

    qDebug() << "Default cube loaded. Vertices:" << m_customVertices.size()
             << "Indices:" << m_indexCount;
}

void CubeRenderer::recreateBuffers()
{
    // Освобождаем старые ресурсы если они существуют
    if (m_vbuf != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyBuffer(m_dev, m_vbuf, nullptr);
        m_vbuf = VK_NULL_HANDLE;
    }
    if (m_vbufMem != VK_NULL_HANDLE) {
        m_devFuncs->vkFreeMemory(m_dev, m_vbufMem, nullptr);
        m_vbufMem = VK_NULL_HANDLE;
    }
    if (m_ibuf != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyBuffer(m_dev, m_ibuf, nullptr);
        m_ibuf = VK_NULL_HANDLE;
    }
    if (m_ibufMem != VK_NULL_HANDLE) {
        m_devFuncs->vkFreeMemory(m_dev, m_ibufMem, nullptr);
        m_ibufMem = VK_NULL_HANDLE;
    }

    VkPhysicalDeviceProperties physDevProps;
    m_funcs->vkGetPhysicalDeviceProperties(m_physDev, &physDevProps);

    VkPhysicalDeviceMemoryProperties physDevMemProps;
    m_funcs->vkGetPhysicalDeviceMemoryProperties(m_physDev, &physDevMemProps);

    // Vertex buffer
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_customVertices.size() * sizeof(VertexData);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_vbuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_vbuf, &memReq);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;

    uint32_t memTypeIndex = findMemoryType(memReq.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           physDevMemProps);

    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find suitable memory type for vertex buffer");

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

    memcpy(p, m_customVertices.constData(), bufferInfo.size);
    m_devFuncs->vkUnmapMemory(m_dev, m_vbufMem);

    // Index buffer - правильный размер для текущей модели
    bufferInfo.size = m_customIndices.size() * sizeof(uint32_t); // ВСЕГДА uint32_t
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_ibuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create index buffer: %d", err);

    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_ibuf, &memReq);
    allocInfo.allocationSize = memReq.size;

    memTypeIndex = findMemoryType(memReq.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  physDevMemProps);

    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find suitable memory type for index buffer");

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

    memcpy(p, m_customIndices.constData(), bufferInfo.size);
    m_devFuncs->vkUnmapMemory(m_dev, m_ibufMem); // ВАЖНО: не забыть!
}

void CubeRenderer::destroyTexture()
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

void CubeRenderer::loadCustomModel(const QString& filePath)
{
    m_customVertices.clear();
    m_customIndices.clear();

    if (m_modelLoader.loadOBJ(filePath, m_customVertices, m_customIndices)) {
        recreateBuffers();
        m_indexCount = m_customIndices.size();
        m_useCustomModel = true;
        qDebug() << "Custom model loaded successfully. Vertices:"
                 << m_customVertices.size() << "Indices:" << m_indexCount;
    } else {
        qWarning() << "Failed to load model, falling back to cube";
        loadDefaultCube();
    }
}

void VulkanCube::sync()
{
    if (!m_renderer) {
        m_renderer = new CubeRenderer;
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &CubeRenderer::frameStart, Qt::DirectConnection);
        connect(window(), &QQuickWindow::beforeRenderPassRecording, m_renderer, &CubeRenderer::mainPassRecordingStart, Qt::DirectConnection);

        // Подключаем сигнал загрузки модели
        connect(this, &VulkanCube::modelLoaded, this, [this]() {
            // Оповещаем о загрузке модели, но саму загрузку теперь делаем в loadModel
            if (window())
                window()->update();
        });

        // Подключаем сигнал загрузки куба по умолчанию
        connect(m_renderer, &CubeRenderer::needLoadDefaultCube, this, [this]() {
            if (m_renderer) {
                m_renderer->loadDefaultCube();
            }
        });
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
    m_renderer->setT(m_t);
    m_renderer->setCubePosition(m_cubePositionX, m_cubePositionY, m_cubePositionZ);
    m_renderer->setWindow(window());
    m_renderer->setUseCustomModel(m_useCustomModel);
}

void CubeRenderer::frameStart()
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

// Вершины куба с позицией, текстурными координатами и нормалями
struct Vertex {
    float pos[3];
    float texCoord[2];
    float normal[3];
};

static const Vertex vertices[] = {
    // Передняя грань (Z+)
    {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{1.0f, -1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    {{-1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    // Задняя грань (Z-)
    {{1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    {{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},

    // Левая грань (X-)
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
    {{-1.0f, -1.0f, 1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
    {{-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
    {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},

    // Правая грань (X+)
    {{1.0f, -1.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{1.0f, 1.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
    {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},

    // Верхняя грань (Y+)
    {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{1.0f, 1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},

    // Нижняя грань (Y-)
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    {{1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
    {{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}}
};

static const uint16_t indices[] = {
    // Передняя грань
    0, 1, 2, 2, 3, 0,
    // Задняя грань
    4, 5, 6, 6, 7, 4,
    // Левая грань
    8, 9, 10, 10, 11, 8,
    // Правая грань
    12, 13, 14, 14, 15, 12,
    // Верхняя грань
    16, 17, 18, 18, 19, 16,
    // Нижняя грань
    20, 21, 22, 22, 23, 20
};

const int UBUF_SIZE = sizeof(float) * 16 * 3 + sizeof(float); // 3 матрицы 4x4 + время

void CubeRenderer::mainPassRecordingStart()
{
    const QQuickWindow::GraphicsStateInfo &stateInfo(m_window->graphicsStateInfo());
    QSGRendererInterface *rif = m_window->rendererInterface();
    // Обновляем uniform buffer
    VkDeviceSize ubufOffset = stateInfo.currentFrameSlot * m_allocPerUbuf;
    void *p = nullptr;
    VkResult err = m_devFuncs->vkMapMemory(m_dev, m_ubufMem, ubufOffset, m_allocPerUbuf, 0, &p);
    if (err != VK_SUCCESS || !p)
        qFatal("Failed to map uniform buffer memory: %d", err);

    // Матрицы для 3D преобразований с вращением и позицией
    QMatrix4x4 model;

    // Сначала перемещаем куб в позицию, заданную пользователем
    model.translate(m_cubePositionX, m_cubePositionY, m_cubePositionZ);

    // Затем применяем вращение вокруг своей оси
    float angle = m_t * 360.0f; // Полный оборот за 1 секунду
    model.rotate(angle, QVector3D(1.0f, 0.0f, 0.0f)); // Вращение вокруг X
    model.rotate(angle * 0.7f, QVector3D(0.0f, 0.0f, 1.0f)); // Вращение вокруг Z

    QMatrix4x4 view;
    // Камера смотрит на центр сцены
    view.lookAt(QVector3D(0.0f, 0.0f, 10.0f),  // позиция камеры
                QVector3D(0.0f, 0.0f, 0.0f),   // цель (центр сцены)
                QVector3D(0.0f, 1.0f, 0.0f));  // вектор "вверх"

    QMatrix4x4 proj;
    proj.perspective(60.0f, m_viewportSize.width() / (float)m_viewportSize.height(), 0.1f, 15000.0f);

    // Копируем матрицы и время в uniform buffer
    float *data = static_cast<float*>(p);
    memcpy(data, model.constData(), 16 * sizeof(float));
    memcpy(data + 16, view.constData(), 16 * sizeof(float));
    memcpy(data + 32, proj.constData(), 16 * sizeof(float));
    data[48] = m_t * 10.0f; // Ускоряем анимацию

    m_devFuncs->vkUnmapMemory(m_dev, m_ubufMem);

    m_window->beginExternalCommands();

    VkCommandBuffer cb = *reinterpret_cast<VkCommandBuffer *>(
        rif->getResource(m_window, QSGRendererInterface::CommandListResource));
    Q_ASSERT(cb);

    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkDeviceSize vbufOffset = 0;
    m_devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &m_vbuf, &vbufOffset);

    // ИСПРАВЛЕНО: Всегда используем uint32_t для индексов
    m_devFuncs->vkCmdBindIndexBuffer(cb, m_ibuf, 0, VK_INDEX_TYPE_UINT32);

    uint32_t dynamicOffset = m_allocPerUbuf * stateInfo.currentFrameSlot;
    m_devFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                                        &m_ubufDescriptor, 1, &dynamicOffset);

    VkViewport vp = { 0, 0, float(m_viewportSize.width()), float(m_viewportSize.height()), 0.0f, 1.0f };
    m_devFuncs->vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor = { { 0, 0 }, { uint32_t(m_viewportSize.width()), uint32_t(m_viewportSize.height()) } };
    m_devFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

    m_devFuncs->vkCmdDrawIndexed(cb, m_indexCount, 1, 0, 0, 0);

    m_window->endExternalCommands();
}

void CubeRenderer::prepareShader(Stage stage)
{
    QString filename;
    if (stage == VertexStage) {
        filename = QLatin1String(":/cube.vert.spv");
    } else {
        Q_ASSERT(stage == FragmentStage);
        filename = QLatin1String(":/cube.frag.spv");
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

void CubeRenderer::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
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

void CubeRenderer::copyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
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

void CubeRenderer::loadTexture()
{
    // Загружаем текстуру из файла
    QImage image(":/textures/metalplate01_rgba.png");
    qDebug() << image.isNull();
    if (image.isNull()) {
        // Если текстура не загружена, создаем простую текстуру программно
        image = QImage(256, 256, QImage::Format_RGBA8888);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QColor color;
                if ((x / 32 + y / 32) % 2 == 0) {
                    color = QColor(0, 255, 255); // Cornflower blue
                } else {
                    color = QColor(255, 0, 0); // White
                }
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
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
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
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
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

void CubeRenderer::init(int framesInFlight)
{
    Q_ASSERT(framesInFlight <= 3);
    m_initialized = true;

    // Получаем Vulkan ресурсы из QQuickWindow
    QSGRendererInterface *rif = m_window->rendererInterface();
    QVulkanInstance *inst = reinterpret_cast<QVulkanInstance *>(
        rif->getResource(m_window, QSGRendererInterface::VulkanInstanceResource));
    Q_ASSERT(inst && inst->isValid());

    m_physDev = *reinterpret_cast<VkPhysicalDevice *>(
        rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
    m_dev = *reinterpret_cast<VkDevice *>(
        rif->getResource(m_window, QSGRendererInterface::DeviceResource));
    Q_ASSERT(m_physDev && m_dev);

    m_devFuncs = inst->deviceFunctions(m_dev);
    m_funcs = inst->functions();
    Q_ASSERT(m_devFuncs && m_funcs);

    VkRenderPass rp = *reinterpret_cast<VkRenderPass *>(
        rif->getResource(m_window, QSGRendererInterface::RenderPassResource));
    Q_ASSERT(rp);

    // Загружаем текстуру
    loadTexture();

    // ВСЕГДА загружаем геометрию куба по умолчанию при инициализации
    if (!m_modelLoader.loadBuiltInCube(m_customVertices, m_customIndices)) {
        qFatal("Failed to load built-in cube");
    }

    m_useCustomModel = false; // По умолчанию используем куб
    m_indexCount = m_customIndices.size();

    qDebug() << "Loaded built-in cube. Vertices:" << m_customVertices.size()
             << "Indices:" << m_customIndices.size();

    // Получаем свойства устройства и памяти
    VkPhysicalDeviceProperties physDevProps;
    m_funcs->vkGetPhysicalDeviceProperties(m_physDev, &physDevProps);

    VkPhysicalDeviceMemoryProperties physDevMemProps;
    m_funcs->vkGetPhysicalDeviceMemoryProperties(m_physDev, &physDevMemProps);

    // Флаги памяти для буферов
    const VkMemoryPropertyFlags hostVisibleMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // 1. Vertex Buffer
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_customVertices.size() * sizeof(VertexData);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_vbuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_vbuf, &memReq);

    uint32_t memTypeIndex = findMemoryType(memReq.memoryTypeBits, hostVisibleMemory, physDevMemProps);
    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find suitable memory type for vertex buffer");

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_vbufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate vertex buffer memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(m_dev, m_vbuf, m_vbufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);

    // Заполняем vertex buffer данными
    void *data;
    err = m_devFuncs->vkMapMemory(m_dev, m_vbufMem, 0, bufferInfo.size, 0, &data);
    if (err != VK_SUCCESS)
        qFatal("Failed to map vertex buffer memory: %d", err);

    memcpy(data, m_customVertices.constData(), bufferInfo.size);
    m_devFuncs->vkUnmapMemory(m_dev, m_vbufMem);

    // 2. Index Buffer - ВСЕГДА uint32_t
    bufferInfo.size = m_customIndices.size() * sizeof(uint32_t); // <-- ИСПРАВЛЕНО: всегда uint32_t
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_ibuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create index buffer: %d", err);

    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_ibuf, &memReq);
    memTypeIndex = findMemoryType(memReq.memoryTypeBits, hostVisibleMemory, physDevMemProps);
    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find suitable memory type for index buffer");

    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_ibufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate index buffer memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(m_dev, m_ibuf, m_ibufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind index buffer memory: %d", err);

    err = m_devFuncs->vkMapMemory(m_dev, m_ibufMem, 0, bufferInfo.size, 0, &data);
    if (err != VK_SUCCESS)
        qFatal("Failed to map index buffer memory: %d", err);

    // Копируем индексы без конвертации (они уже uint32_t)
    memcpy(data, m_customIndices.constData(), bufferInfo.size);
    m_devFuncs->vkUnmapMemory(m_dev, m_ibufMem); // ВАЖНО: не забыть!

    // 3. Uniform Buffer
    const VkDeviceSize ubufAlign = physDevProps.limits.minUniformBufferOffsetAlignment;
    m_allocPerUbuf = aligned(UBUF_SIZE, ubufAlign);

    bufferInfo.size = m_allocPerUbuf * framesInFlight;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    err = m_devFuncs->vkCreateBuffer(m_dev, &bufferInfo, nullptr, &m_ubuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create uniform buffer: %d", err);

    m_devFuncs->vkGetBufferMemoryRequirements(m_dev, m_ubuf, &memReq);
    memTypeIndex = findMemoryType(memReq.memoryTypeBits, hostVisibleMemory, physDevMemProps);
    if (memTypeIndex == uint32_t(-1))
        qFatal("Failed to find suitable memory type for uniform buffer");

    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    err = m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_ubufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate uniform buffer memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(m_dev, m_ubuf, m_ubufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind uniform buffer memory: %d", err);

    // 4. Descriptor Set Layout
    VkDescriptorSetLayoutBinding layoutBindings[2] = {};

    // Uniform buffer binding
    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBindings[0].pImmutableSamplers = nullptr;

    // Texture sampler binding
    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = layoutBindings;

    err = m_devFuncs->vkCreateDescriptorSetLayout(m_dev, &layoutInfo, nullptr, &m_resLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor set layout: %d", err);

    // 5. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_resLayout;

    err = m_devFuncs->vkCreatePipelineLayout(m_dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    // 6. Descriptor Pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    err = m_devFuncs->vkCreateDescriptorPool(m_dev, &poolInfo, nullptr, &m_descriptorPool);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor pool: %d", err);

    // 7. Descriptor Set
    VkDescriptorSetAllocateInfo allocSetInfo = {};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = m_descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &m_resLayout;

    err = m_devFuncs->vkAllocateDescriptorSets(m_dev, &allocSetInfo, &m_ubufDescriptor);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate descriptor set: %d", err);

    // 8. Update Descriptor Set
    VkDescriptorBufferInfo bufferDescInfo = {};
    bufferDescInfo.buffer = m_ubuf;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = UBUF_SIZE;

    VkDescriptorImageInfo imageDescInfo = {};
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescInfo.imageView = m_texture.view;
    imageDescInfo.sampler = m_texture.sampler;

    VkWriteDescriptorSet descriptorWrites[2] = {};

    // Uniform buffer descriptor
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_ubufDescriptor;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferDescInfo;

    // Texture sampler descriptor
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_ubufDescriptor;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageDescInfo;

    m_devFuncs->vkUpdateDescriptorSets(m_dev, 2, descriptorWrites, 0, nullptr);

    // 9. Pipeline Cache
    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    err = m_devFuncs->vkCreatePipelineCache(m_dev, &cacheInfo, nullptr, &m_pipelineCache);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline cache: %d", err);

    // 10. Create Graphics Pipeline
    VkShaderModule vertShaderModule = createShaderModule(m_vert);
    VkShaderModule fragShaderModule = createShaderModule(m_frag);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};

    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShaderModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShaderModule;
    shaderStages[1].pName = "main";

    // Vertex input
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VertexData);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[3] = {};

    // Position attribute
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VertexData, position);

    // TexCoord attribute
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VertexData, texCoord);

    // Normal attribute
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(VertexData, normal);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = rp;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    err = m_devFuncs->vkCreateGraphicsPipelines(
        m_dev, m_pipelineCache, 1, &pipelineInfo, nullptr, &m_pipeline);

    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);

    // Cleanup shader modules
    m_devFuncs->vkDestroyShaderModule(m_dev, vertShaderModule, nullptr);
    m_devFuncs->vkDestroyShaderModule(m_dev, fragShaderModule, nullptr);

    qDebug() << "Renderer initialized successfully";
    qDebug() << "Model vertices:" << m_customVertices.size()
             << "indices:" << m_indexCount
             << "using custom model:" << m_useCustomModel;
}

#include "vulkancube.moc"
