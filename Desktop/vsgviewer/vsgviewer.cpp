#include <vsg/all.h>

#ifdef USE_VSGXCHANGE
#include <vsgXchange/ReaderWriter_all.h>
#include <vsgXchange/ShaderCompiler.h>
#endif

#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>

#include "AnimationPath.h"


vsg::ref_ptr<vsg::Node> createTextureQuad(vsg::ref_ptr<vsg::Data> textureData)
{
    // set up search paths to SPIRV shaders and textures
    vsg::Paths searchPaths = vsg::getEnvPaths("VSG_FILE_PATH");

    // load shaders
    vsg::ref_ptr<vsg::ShaderStage> vertexShader = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", vsg::findFile("shaders/vert_PushConstants.spv", searchPaths));
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::findFile("shaders/frag_PushConstants.spv", searchPaths));
    if (!vertexShader || !fragmentShader)
    {
        std::cout<<"Could not create shaders."<<std::endl;
        return {};
    }

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings
    {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr} // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges
    {
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128} // projection view, and model matrices, actual push constant calls autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions
    {
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // colour data
        VkVertexInputBindingDescription{2, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}  // tex coord data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions
    {
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, // colour data
        VkVertexInputAttributeDescription{2, 2, VK_FORMAT_R32G32_SFLOAT, 0},    // tex coord data
    };

    vsg::GraphicsPipelineStates pipelineStates
    {
        vsg::VertexInputState::create( vertexBindingsDescriptions, vertexAttributeDescriptions ),
        vsg::InputAssemblyState::create(),
        vsg::RasterizationState::create(),
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()
    };

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    // create texture image and associated DescriptorSets and binding
    auto texture = vsg::DescriptorImage::create(vsg::Sampler::create(), textureData, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{texture});
    auto bindDescriptorSets = vsg::BindDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->getPipelineLayout(), 0, vsg::DescriptorSets{descriptorSet});

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors to decorate the whole graph
    auto scenegraph = vsg::StateGroup::create();
    scenegraph->add(bindGraphicsPipeline);
    scenegraph->add(bindDescriptorSets);

    // set up model transformation node
    auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    scenegraph->addChild(transform);

    // set up vertex and index arrays
    auto vertices = vsg::vec3Array::create(
    {
        {0.0f, 0.0f, 0.0f},
        {static_cast<float>(textureData->width()), 0.0f, 0.0f},
        {static_cast<float>(textureData->width()), 0.0f, static_cast<float>(textureData->height())},
        {0.0f, 0.0f, static_cast<float>(textureData->height())}
    }); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_INSTANCE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto colors = vsg::vec3Array::create(
    {
        {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}
    }); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    uint8_t origin = textureData->getLayout().origin; // in Vulkan the origin is by default top left.
    float left = 0.0f;
    float right = 1.0f;
    float top = (origin == vsg::TOP_LEFT) ? 0.0f : 1.0f;
    float bottom = (origin == vsg::TOP_LEFT) ? 1.0f : 0.0f;
    auto texcoords = vsg::vec2Array::create(
    {
        {left, bottom},
        {right, bottom},
        {right, top},
        {left, top}
    }); // VK_FORMAT_R32G32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto indices = vsg::ushortArray::create(
    {
        0, 1, 2,
        2, 3, 0
    }); // VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors, texcoords}));
    drawCommands->addChild(vsg::BindIndexBuffer::create(indices));
    drawCommands->addChild(vsg::DrawIndexed::create(6, 1, 0, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    return scenegraph;
}


int main(int argc, char** argv)
{
    // set up defaults and read command line arguments to override them
    auto options = vsg::Options::create();
    auto windowTraits = vsg::WindowTraits::create();
    windowTraits->windowTitle = "vsgviewer";

    // set up defaults and read command line arguments to override them
    vsg::CommandLine arguments(&argc, argv);
    windowTraits->debugLayer = arguments.read({"--debug","-d"});
    windowTraits->apiDumpLayer = arguments.read({"--api","-a"});
    if (arguments.read("--IMMEDIATE")) windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (arguments.read("--FIFO")) windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (arguments.read("--FIFO_RELAXED")) windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    if (arguments.read("--MAILBOX")) windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    if (arguments.read({"-t", "--test"})) { windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; windowTraits->fullscreen = true; }
    if (arguments.read({"--st", "--small-test"})) { windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; windowTraits->width = 192, windowTraits->height = 108; windowTraits->decoration = false; }
    if (arguments.read({"--fullscreen", "--fs"})) windowTraits->fullscreen = true;
    if (arguments.read({"--window", "-w"}, windowTraits->width, windowTraits->height)) { windowTraits->fullscreen = false; }
    if (arguments.read({"--no-frame", "--nf"})) windowTraits->decoration = false;
    if (arguments.read("--or")) windowTraits->overrideRedirect = true;
    auto numFrames = arguments.value(-1, "-f");
    auto pathFilename = arguments.value(std::string(),"-p");
    auto loadLevels = arguments.value(0, "--load-levels");
    auto horizonMountainHeight = arguments.value(0.0, "--hmh");
    auto useDatabasePager = arguments.read("--pager");
    auto maxPageLOD = arguments.value(-1, "--max-plod");
    arguments.read("--screen", windowTraits->screenNum);
    arguments.read("--display", windowTraits->display);
    // Interpret the samples directly as a VkSampleCountFlagBits
    // enum. Kind of gross, but it works.
    arguments.read("--samples", windowTraits->samples);

    if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

#ifdef USE_VSGXCHANGE
    // add use of vsgXchange's support for reading and writing 3rd party file formats
    options->readerWriter = vsgXchange::ReaderWriter_all::create();
#endif


    auto group = vsg::Group::create();

    vsg::Path path;

    // read any vsg files
    for (int i=1; i<argc; ++i)
    {
        vsg::Path filename = arguments[i];
        path = vsg::filePath(filename);

        auto object = vsg::read(filename, options);
        if (auto node = object.cast<vsg::Node>(); node)
        {
            group->addChild(node);
        }
        else if (auto data = object.cast<vsg::Data>(); data)
        {
            if (auto node = createTextureQuad(data); node)
            {
                group->addChild(node);
            }
        }
        else if (object)
        {
            std::cout<<"Unable to view object of type "<<object->className()<<std::endl;
        }
        else
        {
            std::cout<<"Unable to load model from file "<<filename<<std::endl;
        }
    }

    if (group->getNumChildren()==0)
    {
        std::cout<<"Please specify a 3d model file on the command line."<<std::endl;
        return 1;
    }

    vsg::ref_ptr<vsg::Node> vsg_scene;
    if (group->getChildren().size()==1) vsg_scene = group->getChild(0);
    else vsg_scene = group;

    // create the viewer and assign window(s) to it
    auto viewer = vsg::Viewer::create();

    vsg::ref_ptr<vsg::Window> window(vsg::Window::create(windowTraits));
    if (!window)
    {
        std::cout<<"Could not create windows."<<std::endl;
        return 1;
    }

    viewer->addWindow(window);

    // compute the bounds of the scene graph to help position camera
    vsg::ComputeBounds computeBounds;
    vsg_scene->accept(computeBounds);
    vsg::dvec3 centre = (computeBounds.bounds.min+computeBounds.bounds.max)*0.5;
    double radius = vsg::length(computeBounds.bounds.max-computeBounds.bounds.min)*0.6;
    double nearFarRatio = 0.001;

    // set up the camera
    auto lookAt = vsg::LookAt::create(centre+vsg::dvec3(0.0, -radius*3.5, 0.0), centre, vsg::dvec3(0.0, 0.0, 1.0));

    vsg::ref_ptr<vsg::ProjectionMatrix> perspective;
    if (vsg::ref_ptr<vsg::EllipsoidModel> ellipsoidModel(vsg_scene->getObject<vsg::EllipsoidModel>("EllipsoidModel")); ellipsoidModel)
    {
        perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio, horizonMountainHeight);
    }
    else
    {
        perspective = vsg::Perspective::create(30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio*radius, radius * 4.5);
    }

    auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

    // set up database pager
    vsg::ref_ptr<vsg::DatabasePager> databasePager;
    if (useDatabasePager)
    {
        databasePager = vsg::DatabasePager::create();
        if (maxPageLOD>=0) databasePager->targetMaxNumPagedLODWithHighResSubgraphs = maxPageLOD;
    }

    // add close handler to respond the close window button and pressing escape
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    if (pathFilename.empty())
    {
        viewer->addEventHandler(vsg::Trackball::create(camera));
    }
    else
    {
        std::ifstream in(pathFilename);
        if (!in)
        {
            std::cout << "AnimationPat: Could not open animation path file \"" << pathFilename << "\".\n";
            return 1;
        }

        vsg::ref_ptr<vsg::AnimationPath> animationPath(new vsg::AnimationPath);
        animationPath->read(in);

        viewer->addEventHandler(vsg::AnimationPathHandler::create(camera, animationPath, viewer->start_point()));
    }

    // if required pre load specific number of PagedLOD levels.
    if (loadLevels > 0)
    {
        vsg::LoadPagedLOD loadPagedLOD(camera, loadLevels);

        auto startTime =std::chrono::steady_clock::now();

        vsg_scene->accept(loadPagedLOD);

        auto time = std::chrono::duration<float, std::chrono::milliseconds::period>(std::chrono::steady_clock::now()-startTime).count();
        std::cout<<"No. of tiles loaed "<<loadPagedLOD.numTiles<<" in "<<time<<"ms."<<std::endl;
    }

    auto commandGraph = vsg::createCommandGraphForView(window, camera, vsg_scene);
    viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph}, databasePager);

    viewer->compile();

    // rendering main loop
    while (viewer->advanceToNextFrame() && (numFrames<0 || (numFrames--)>0))
    {
        // pass any events into EventHandlers assigned to the Viewer
        viewer->handleEvents();

        viewer->update();

        viewer->recordAndSubmit();

        viewer->present();
    }

    // clean up done automatically thanks to ref_ptr<>
    return 0;
}
