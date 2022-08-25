// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2022 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Radu Serban, Rainer Gericke
// =============================================================================

#include "ShapeBuilder.h"
#include "GetBoxShapeData.h"
#include "GetDiceShapeData.h"
#include "GetSphereShapeData.h"
#include "GetParticleShapeData.h"
#include "GetCylinderShapeData.h"
#include "GetCapsuleShapeData.h"
#include "GetConeShapeData.h"
#include "GetTriangleMeshShapeData.h"
#include "GetSurfaceShapeData.h"

#include "chrono_vsg/resources/lineShader_vert.h"
#include "chrono_vsg/resources/lineShader_frag.h"

namespace chrono {
namespace vsg3d {
void ShapeBuilder::assignCompileTraversal(vsg::ref_ptr<vsg::CompileTraversal> ct) {
    compileTraversal = ct;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createShape(BasicShape theShape,
                                                   std::shared_ptr<ChPhysicsItem> physItem,
                                                   ChVisualModel::ShapeInstance shapeInstance,
                                                   std::shared_ptr<ChVisualMaterial> material,
                                                   vsg::ref_ptr<vsg::MatrixTransform> transform,
                                                   bool drawMode,
                                                   std::shared_ptr<ChTriangleMeshShape> tms,
                                                   std::shared_ptr<ChSurfaceShape> surface) {
    auto scenegraph = vsg::Group::create();
    // store some information for easier update
    scenegraph->setValue("ItemPtr", physItem);
    scenegraph->setValue("ShapeInstancePtr", shapeInstance);
    scenegraph->setValue("TransformPtr", transform);

    vsg::ref_ptr<vsg::ShaderSet> shaderSet;

    auto repeatValues = vsg::vec3Value::create();
    repeatValues->set(vsg::vec3(material->GetKdTextureScale().x(), material->GetKdTextureScale().y(), 1.0f));
    shaderSet = createTilingPhongShaderSet(m_options);

    auto rasterizationState = vsg::RasterizationState::create();
    if (drawMode) {
        rasterizationState->polygonMode = VK_POLYGON_MODE_LINE;
    }
    shaderSet->defaultGraphicsPipelineStates.push_back(rasterizationState);
    auto graphicsPipelineConfig = vsg::GraphicsPipelineConfig::create(shaderSet);
    auto& defines = graphicsPipelineConfig->shaderHints->defines;

    if (theShape == TRIANGLE_MESH_SHAPE) {
        // two-sided polygons? -> cannot be used together with transparency!
        if (!tms->IsBackfaceCull() && material->GetOpacity() == 1.0) {
            graphicsPipelineConfig->rasterizationState->cullMode = VK_CULL_MODE_NONE;
            defines.push_back("VSG_TWO_SIDED_LIGHTING");
        }
    }

    // set up graphics pipeline
    vsg::Descriptors descriptors;

    // set up pass of material
    auto phongMat = vsg::PhongMaterialValue::create();
    float alpha = material->GetOpacity();
    phongMat->value().diffuse.set(material->GetDiffuseColor().R, material->GetDiffuseColor().G,
                                  material->GetDiffuseColor().B, alpha);
    phongMat->value().ambient.set(material->GetAmbientColor().R, material->GetAmbientColor().G,
                                  material->GetAmbientColor().B, alpha);
    phongMat->value().specular.set(material->GetSpecularColor().R, material->GetSpecularColor().G,
                                   material->GetSpecularColor().B, alpha);
    phongMat->value().emissive.set(material->GetEmissiveColor().R, material->GetEmissiveColor().G,
                                   material->GetEmissiveColor().B, alpha);
    phongMat->value().alphaMask = alpha;
    phongMat->value().alphaMaskCutoff = 0.3f;

    // read texture image
    vsg::Path textureFile(material->GetKdTexture());
    if (textureFile) {
        auto textureData = vsg::read_cast<vsg::Data>(textureFile, m_options);
        if (!textureData) {
            std::cout << "Could not read texture file : " << textureFile << std::endl;
        }
        // enable texturing with anisotrpy filtering
        auto sampler = vsg::Sampler::create();
        sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // default yet, just an example how to set
        sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler->anisotropyEnable = VK_TRUE;
        sampler->maxAnisotropy = m_maxAnisotropy;
        graphicsPipelineConfig->assignTexture(descriptors, "diffuseMap", textureData, sampler);
        // vsg combines material color and texture color, better use only one of it
        phongMat->value().diffuse.set(1.0, 1.0, 1.0, alpha);
    }

    // set transparency, if needed
    vsg::ColorBlendState::ColorBlendAttachments colorBlendAttachments;
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_FALSE;  // default
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (phongMat->value().alphaMask < 1.0) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    colorBlendAttachments.push_back(colorBlendAttachment);
    graphicsPipelineConfig->colorBlendState = vsg::ColorBlendState::create(colorBlendAttachments);
    graphicsPipelineConfig->assignUniform(descriptors, "texrepeat", repeatValues);
    graphicsPipelineConfig->assignUniform(descriptors, "material", phongMat);

    if (m_options->sharedObjects)
        m_options->sharedObjects->share(descriptors);

    vsg::ref_ptr<vsg::vec3Array> vertices;
    vsg::ref_ptr<vsg::vec3Array> normals;
    vsg::ref_ptr<vsg::vec2Array> texcoords;
    vsg::ref_ptr<vsg::ushortArray> indices;
    float boundingSphereRadius;
    switch (theShape) {
        case BOX_SHAPE:
            GetBoxShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
        case DICE_SHAPE:
            GetDiceShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
        case SPHERE_SHAPE:
            GetSphereShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
        case CYLINDER_SHAPE:
            GetCylinderShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
        case CAPSULE_SHAPE:
            GetCapsuleShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
        case CONE_SHAPE:
            GetConeShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
        case TRIANGLE_MESH_SHAPE:
            GetTriangleMeshShapeData(tms, vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
        case SURFACE_SHAPE:
            GetSurfaceShapeData(surface, vertices, normals, texcoords, indices, boundingSphereRadius);
            break;
    }
    auto colors = vsg::vec4Value::create(vsg::vec4{1.0f, 1.0f, 1.0f, 1.0f});

    vsg::DataList vertexArrays;

    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, vertices);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, normals);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_TexCoord0", VK_VERTEX_INPUT_RATE_VERTEX, texcoords);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Color", VK_VERTEX_INPUT_RATE_INSTANCE, colors);

    if (m_options->sharedObjects)
        m_options->sharedObjects->share(vertexArrays);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(indices);

    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(graphicsPipelineConfig->baseAttributeBinding, vertexArrays));
    drawCommands->addChild(vsg::BindIndexBuffer::create(indices));
    drawCommands->addChild(vsg::DrawIndexed::create(indices->size(), 1, 0, 0, 0));

    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(drawCommands->children);
        m_options->sharedObjects->share(drawCommands);
    }

    // register the ViewDescriptorSetLayout.
    vsg::ref_ptr<vsg::ViewDescriptorSetLayout> vdsl;
    if (m_options->sharedObjects)
        vdsl = m_options->sharedObjects->shared_default<vsg::ViewDescriptorSetLayout>();
    else
        vdsl = vsg::ViewDescriptorSetLayout::create();
    graphicsPipelineConfig->additionalDescrptorSetLayout = vdsl;

    // share the pipeline config and initialize if it's unique
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(graphicsPipelineConfig, [](auto gpc) { gpc->init(); });
    else
        graphicsPipelineConfig->init();

    auto descriptorSet = vsg::DescriptorSet::create(graphicsPipelineConfig->descriptorSetLayout, descriptors);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(descriptorSet);

    auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                            graphicsPipelineConfig->layout, 0, descriptorSet);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(bindDescriptorSet);

    auto bindViewDescriptorSets =
        vsg::BindViewDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineConfig->layout, 1);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(bindViewDescriptorSets);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(graphicsPipelineConfig->bindGraphicsPipeline);
    stateGroup->add(bindDescriptorSet);
    stateGroup->add(bindViewDescriptorSets);

    // set up model transformation node
    transform->subgraphRequiresLocalFrustum = false;

    // add drawCommands to StateGroup
    stateGroup->addChild(drawCommands);
    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(stateGroup);
    }
    transform->addChild(stateGroup);

    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(transform);
    }

    scenegraph->addChild(transform);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);

    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createParticleShape(std::shared_ptr<ChVisualMaterial> material,
                                                           vsg::ref_ptr<vsg::MatrixTransform> transform,
                                                           bool drawMode) {
    auto scenegraph = vsg::Group::create();
    // store some information for easier update
    scenegraph->setValue("TransformPtr", transform);

    auto shaderSet = vsg::createPhongShaderSet(m_options);
    auto rasterizationState = vsg::RasterizationState::create();
    if (drawMode) {
        rasterizationState->polygonMode = VK_POLYGON_MODE_LINE;
    }
    shaderSet->defaultGraphicsPipelineStates.push_back(rasterizationState);
    auto graphicsPipelineConfig = vsg::GraphicsPipelineConfig::create(shaderSet);
    auto& defines = graphicsPipelineConfig->shaderHints->defines;

    // set up graphics pipeline
    vsg::Descriptors descriptors;

    // set up pass of material
    auto phongMat = vsg::PhongMaterialValue::create();
    float alpha = material->GetOpacity();
    phongMat->value().diffuse.set(material->GetDiffuseColor().R, material->GetDiffuseColor().G,
                                  material->GetDiffuseColor().B, alpha);
    phongMat->value().ambient.set(material->GetAmbientColor().R, material->GetAmbientColor().G,
                                  material->GetAmbientColor().B, alpha);
    phongMat->value().specular.set(material->GetSpecularColor().R, material->GetSpecularColor().G,
                                   material->GetSpecularColor().B, alpha);
    phongMat->value().emissive.set(material->GetEmissiveColor().R, material->GetEmissiveColor().G,
                                   material->GetEmissiveColor().B, alpha);
    phongMat->value().alphaMask = alpha;
    phongMat->value().alphaMaskCutoff = 0.3f;

    // read texture image
    vsg::Path textureFile(material->GetKdTexture());
    if (textureFile) {
        auto textureData = vsg::read_cast<vsg::Data>(textureFile, m_options);
        if (!textureData) {
            std::cout << "Could not read texture file : " << textureFile << std::endl;
        }
        // enable texturing
        graphicsPipelineConfig->assignTexture(descriptors, "diffuseMap", textureData);
    }

    // set transparency, if needed
    vsg::ColorBlendState::ColorBlendAttachments colorBlendAttachments;
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_FALSE;  // default
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (phongMat->value().alphaMask < 1.0) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    colorBlendAttachments.push_back(colorBlendAttachment);
    graphicsPipelineConfig->colorBlendState = vsg::ColorBlendState::create(colorBlendAttachments);
    graphicsPipelineConfig->assignUniform(descriptors, "material", phongMat);

    if (m_options->sharedObjects)
        m_options->sharedObjects->share(descriptors);

    vsg::ref_ptr<vsg::vec3Array> vertices;
    vsg::ref_ptr<vsg::vec3Array> normals;
    vsg::ref_ptr<vsg::vec2Array> texcoords;
    vsg::ref_ptr<vsg::ushortArray> indices;
    float boundingSphereRadius;

    GetParticleShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
    auto colors = vsg::vec4Value::create(vsg::vec4{1.0f, 1.0f, 1.0f, 1.0f});

    vsg::DataList vertexArrays;

    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, vertices);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, normals);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_TexCoord0", VK_VERTEX_INPUT_RATE_VERTEX, texcoords);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Color", VK_VERTEX_INPUT_RATE_INSTANCE, colors);

    if (m_options->sharedObjects)
        m_options->sharedObjects->share(vertexArrays);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(indices);

    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(graphicsPipelineConfig->baseAttributeBinding, vertexArrays));
    drawCommands->addChild(vsg::BindIndexBuffer::create(indices));
    drawCommands->addChild(vsg::DrawIndexed::create(indices->size(), 1, 0, 0, 0));

    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(drawCommands->children);
        m_options->sharedObjects->share(drawCommands);
    }

    // register the ViewDescriptorSetLayout.
    vsg::ref_ptr<vsg::ViewDescriptorSetLayout> vdsl;
    if (m_options->sharedObjects)
        vdsl = m_options->sharedObjects->shared_default<vsg::ViewDescriptorSetLayout>();
    else
        vdsl = vsg::ViewDescriptorSetLayout::create();
    graphicsPipelineConfig->additionalDescrptorSetLayout = vdsl;

    // share the pipeline config and initialize if it's unique
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(graphicsPipelineConfig, [](auto gpc) { gpc->init(); });
    else
        graphicsPipelineConfig->init();

    auto descriptorSet = vsg::DescriptorSet::create(graphicsPipelineConfig->descriptorSetLayout, descriptors);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(descriptorSet);

    auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                            graphicsPipelineConfig->layout, 0, descriptorSet);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(bindDescriptorSet);

    auto bindViewDescriptorSets =
        vsg::BindViewDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineConfig->layout, 1);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(bindViewDescriptorSets);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(graphicsPipelineConfig->bindGraphicsPipeline);
    stateGroup->add(bindDescriptorSet);
    stateGroup->add(bindViewDescriptorSets);

    // set up model transformation node
    transform->subgraphRequiresLocalFrustum = false;

    // add drawCommands to StateGroup
    stateGroup->addChild(drawCommands);
    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(stateGroup);
    }
    transform->addChild(stateGroup);

    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(transform);
    }

    scenegraph->addChild(transform);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);

    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createParticlePattern(std::shared_ptr<ChVisualMaterial> material,
                                                             bool drawMode) {
    auto scenegraph = vsg::Group::create();

    auto shaderSet = vsg::createPhongShaderSet(m_options);
    auto rasterizationState = vsg::RasterizationState::create();
    if (drawMode) {
        rasterizationState->polygonMode = VK_POLYGON_MODE_LINE;
    }
    shaderSet->defaultGraphicsPipelineStates.push_back(rasterizationState);
    auto graphicsPipelineConfig = vsg::GraphicsPipelineConfig::create(shaderSet);
    auto& defines = graphicsPipelineConfig->shaderHints->defines;

    // set up graphics pipeline
    vsg::Descriptors descriptors;

    // set up pass of material
    auto phongMat = vsg::PhongMaterialValue::create();
    float alpha = material->GetOpacity();
    phongMat->value().diffuse.set(material->GetDiffuseColor().R, material->GetDiffuseColor().G,
                                  material->GetDiffuseColor().B, alpha);
    phongMat->value().ambient.set(material->GetAmbientColor().R, material->GetAmbientColor().G,
                                  material->GetAmbientColor().B, alpha);
    phongMat->value().specular.set(material->GetSpecularColor().R, material->GetSpecularColor().G,
                                   material->GetSpecularColor().B, alpha);
    phongMat->value().emissive.set(material->GetEmissiveColor().R, material->GetEmissiveColor().G,
                                   material->GetEmissiveColor().B, alpha);
    phongMat->value().alphaMask = alpha;
    phongMat->value().alphaMaskCutoff = 0.3f;

    // read texture image
    vsg::Path textureFile(material->GetKdTexture());
    if (textureFile) {
        auto textureData = vsg::read_cast<vsg::Data>(textureFile, m_options);
        if (!textureData) {
            std::cout << "Could not read texture file : " << textureFile << std::endl;
        }
        // enable texturing
        graphicsPipelineConfig->assignTexture(descriptors, "diffuseMap", textureData);
    }

    // set transparency, if needed
    vsg::ColorBlendState::ColorBlendAttachments colorBlendAttachments;
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_FALSE;  // default
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (phongMat->value().alphaMask < 1.0) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    colorBlendAttachments.push_back(colorBlendAttachment);
    graphicsPipelineConfig->colorBlendState = vsg::ColorBlendState::create(colorBlendAttachments);
    graphicsPipelineConfig->assignUniform(descriptors, "material", phongMat);

    if (m_options->sharedObjects)
        m_options->sharedObjects->share(descriptors);

    vsg::ref_ptr<vsg::vec3Array> vertices;
    vsg::ref_ptr<vsg::vec3Array> normals;
    vsg::ref_ptr<vsg::vec2Array> texcoords;
    vsg::ref_ptr<vsg::ushortArray> indices;
    float boundingSphereRadius;

    GetParticleShapeData(vertices, normals, texcoords, indices, boundingSphereRadius);
    auto colors = vsg::vec4Value::create(vsg::vec4{1.0f, 1.0f, 1.0f, 1.0f});

    vsg::DataList vertexArrays;

    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, vertices);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, normals);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_TexCoord0", VK_VERTEX_INPUT_RATE_VERTEX, texcoords);
    graphicsPipelineConfig->assignArray(vertexArrays, "vsg_Color", VK_VERTEX_INPUT_RATE_INSTANCE, colors);

    if (m_options->sharedObjects)
        m_options->sharedObjects->share(vertexArrays);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(indices);

    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(graphicsPipelineConfig->baseAttributeBinding, vertexArrays));
    drawCommands->addChild(vsg::BindIndexBuffer::create(indices));
    drawCommands->addChild(vsg::DrawIndexed::create(indices->size(), 1, 0, 0, 0));

    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(drawCommands->children);
        m_options->sharedObjects->share(drawCommands);
    }

    // register the ViewDescriptorSetLayout.
    vsg::ref_ptr<vsg::ViewDescriptorSetLayout> vdsl;
    if (m_options->sharedObjects)
        vdsl = m_options->sharedObjects->shared_default<vsg::ViewDescriptorSetLayout>();
    else
        vdsl = vsg::ViewDescriptorSetLayout::create();
    graphicsPipelineConfig->additionalDescrptorSetLayout = vdsl;

    // share the pipeline config and initialize if it's unique
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(graphicsPipelineConfig, [](auto gpc) { gpc->init(); });
    else
        graphicsPipelineConfig->init();

    auto descriptorSet = vsg::DescriptorSet::create(graphicsPipelineConfig->descriptorSetLayout, descriptors);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(descriptorSet);

    auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                            graphicsPipelineConfig->layout, 0, descriptorSet);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(bindDescriptorSet);

    auto bindViewDescriptorSets =
        vsg::BindViewDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineConfig->layout, 1);
    if (m_options->sharedObjects)
        m_options->sharedObjects->share(bindViewDescriptorSets);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(graphicsPipelineConfig->bindGraphicsPipeline);
    stateGroup->add(bindDescriptorSet);
    stateGroup->add(bindViewDescriptorSets);

    // add drawCommands to StateGroup
    stateGroup->addChild(drawCommands);
    if (m_options->sharedObjects) {
        m_options->sharedObjects->share(stateGroup);
    }
    scenegraph->addChild(stateGroup);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);

    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createLineShape(std::shared_ptr<ChPhysicsItem> physItem,
                                                       ChVisualModel::ShapeInstance shapeInstance,
                                                       std::shared_ptr<ChVisualMaterial> material,
                                                       vsg::ref_ptr<vsg::MatrixTransform> transform,
                                                       std::shared_ptr<ChLineShape> ls) {
    auto scenegraph = vsg::Group::create();
    // store some information for easier update
    scenegraph->setValue("ItemPtr", physItem);
    scenegraph->setValue("ShapeInstancePtr", shapeInstance);
    scenegraph->setValue("TransformPtr", transform);

    vsg::ref_ptr<vsg::ShaderStage> vertexShader = lineShader_vert();
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = lineShader_frag();

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr}  // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}  // projection view, and model matrices, actual push constant calls
                                              // autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},  // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}   // colour data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},  // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}   // colour data
    };

    vsg::ref_ptr<vsg::InputAssemblyState> iaState = vsg::InputAssemblyState::create();
    iaState->topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

    vsg::ref_ptr<vsg::RasterizationState> raState = vsg::RasterizationState::create();
    raState->lineWidth = 1.0;  // only allowed value (also set as standard)

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        iaState,
        raState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto pipelineLayout =
        vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline =
        vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    scenegraph->addChild(bindGraphicsPipeline);

    // set up model transformation node
    // auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    scenegraph->addChild(transform);

    // calculate vertices
    int numPoints = ls->GetNumRenderPoints();
    assert(numPoints > 2);
    double ustep = 1.0 / double(numPoints - 1);
    auto vertices = vsg::vec3Array::create(numPoints);
    auto colors = vsg::vec3Array::create(numPoints);
    for (int i = 0; i < numPoints; i++) {
        double u = ustep * (double(i));
        ChVector<> pos;
        ls->GetLineGeometry()->Evaluate(pos, u);
        vertices->set(i, vsg::vec3(pos.x(), pos.y(), pos.z()));
        auto cv =
            vsg::vec3(material->GetDiffuseColor().R, material->GetDiffuseColor().G, material->GetDiffuseColor().B);
        colors->set(i, cv);
    }
    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors}));
    drawCommands->addChild(vsg::Draw::create(vertices->size(), 1, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);
    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createSpringShape(std::shared_ptr<ChLinkBase> linkItem,
                                                         ChVisualModel::ShapeInstance shapeInstance,
                                                         std::shared_ptr<ChVisualMaterial> material,
                                                         vsg::ref_ptr<vsg::MatrixTransform> transform,
                                                         std::shared_ptr<ChSpringShape> ss) {
    auto scenegraph = vsg::Group::create();
    // store some information for easier update
    scenegraph->setValue("LinkPtr", linkItem);
    scenegraph->setValue("ShapeInstancePtr", shapeInstance);
    scenegraph->setValue("TransformPtr", transform);

    vsg::ref_ptr<vsg::ShaderStage> vertexShader = lineShader_vert();
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = lineShader_frag();

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr}  // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}  // projection view, and model matrices, actual push constant calls
                                              // autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},  // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}   // colour data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},  // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}   // colour data
    };

    vsg::ref_ptr<vsg::InputAssemblyState> iaState = vsg::InputAssemblyState::create();
    iaState->topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

    vsg::ref_ptr<vsg::RasterizationState> raState = vsg::RasterizationState::create();
    raState->lineWidth = 1.0;  // only allowed value (also set as standard)

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        iaState,
        raState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto pipelineLayout =
        vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline =
        vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    scenegraph->addChild(bindGraphicsPipeline);

    // set up model transformation node
    // auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    scenegraph->addChild(transform);

    // calculate vertices
    int numPoints = ss->GetResolution();
    double turns = ss->GetTurns();
    assert(numPoints > 2);
    auto vertices = vsg::vec3Array::create(numPoints);
    auto colors = vsg::vec3Array::create(numPoints);
    double length = 1;
    vsg::vec3 p(0, -length / 2, 0);
    double phase = 0.0;
    double height = 0.0;
    for (int iu = 0; iu < numPoints; iu++) {
        phase = turns * CH_C_2PI * (double)iu / (double)numPoints;
        height = length * ((double)iu / (double)numPoints);
        vsg::vec3 pos;
        pos = p + vsg::vec3(cos(phase), height, sin(phase));
        vertices->set(iu, pos);
        auto cv =
            vsg::vec3(material->GetDiffuseColor().R, material->GetDiffuseColor().G, material->GetDiffuseColor().B);
        colors->set(iu, cv);
    }
    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors}));
    drawCommands->addChild(vsg::Draw::create(vertices->size(), 1, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);
    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createCoGSymbol(std::shared_ptr<ChBody> body,
                                                       vsg::ref_ptr<vsg::MatrixTransform> transform) {
    auto scenegraph = vsg::Group::create();
    // store some information for easier update
    scenegraph->setValue("BodyPtr", body);
    scenegraph->setValue("TransformPtr", transform);

    vsg::ref_ptr<vsg::ShaderStage> vertexShader = lineShader_vert();
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = lineShader_frag();

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr}  // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}  // projection view, and model matrices, actual push constant calls
                                              // autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},  // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}   // colour data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},  // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}   // colour data
    };

    vsg::ref_ptr<vsg::InputAssemblyState> iaState = vsg::InputAssemblyState::create();
    iaState->topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    vsg::ref_ptr<vsg::RasterizationState> raState = vsg::RasterizationState::create();
    raState->lineWidth = 1.0;  // only allowed value (also set as standard)

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        iaState,
        raState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto pipelineLayout =
        vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline =
        vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    scenegraph->addChild(bindGraphicsPipeline);

    // set up model transformation node
    // auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    scenegraph->addChild(transform);

    // calculate vertices
    const int numPoints = 6;
    auto vertices = vsg::vec3Array::create(numPoints);
    auto colors = vsg::vec3Array::create(numPoints);
    double length = 1;
    vertices->set(0, vsg::vec3(0.0, 0.0, 0.0));
    vertices->set(1, vsg::vec3(1.0, 0.0, 0.0));
    colors->set(0, vsg::vec3(1.0, 0.0, 0.0));
    colors->set(1, vsg::vec3(1.0, 0.0, 0.0));
    vertices->set(2, vsg::vec3(0.0, 0.0, 0.0));
    vertices->set(3, vsg::vec3(0.0, 1.0, 0.0));
    colors->set(2, vsg::vec3(0.0, 1.0, 0.0));
    colors->set(3, vsg::vec3(0.0, 1.0, 0.0));
    vertices->set(4, vsg::vec3(0.0, 0.0, 0.0));
    vertices->set(5, vsg::vec3(0.0, 0.0, 1.0));
    colors->set(4, vsg::vec3(0.0, 0.0, 1.0));
    colors->set(5, vsg::vec3(0.0, 0.0, 1.0));

    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors}));
    drawCommands->addChild(vsg::Draw::create(vertices->size(), 1, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);
    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createUnitSegment(std::shared_ptr<ChLinkBase> linkItem,
                                                         ChVisualModel::ShapeInstance shapeInstance,
                                                         std::shared_ptr<ChVisualMaterial> material,
                                                         vsg::ref_ptr<vsg::MatrixTransform> transform) {
    auto scenegraph = vsg::Group::create();
    // store some information for easier update
    scenegraph->setValue("LinkPtr", linkItem);
    scenegraph->setValue("ShapeInstancePtr", shapeInstance);
    scenegraph->setValue("TransformPtr", transform);

    vsg::ref_ptr<vsg::ShaderStage> vertexShader = lineShader_vert();
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = lineShader_frag();

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr}  // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}  // projection view, and model matrices, actual push constant calls
                                              // autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},  // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}   // colour data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},  // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}   // colour data
    };

    vsg::ref_ptr<vsg::InputAssemblyState> iaState = vsg::InputAssemblyState::create();
    iaState->topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

    vsg::ref_ptr<vsg::RasterizationState> raState = vsg::RasterizationState::create();
    raState->lineWidth = 1.0;  // only allowed value (also set as standard)

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        iaState,
        raState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto pipelineLayout =
        vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline =
        vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    scenegraph->addChild(bindGraphicsPipeline);

    // set up model transformation node
    // auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    scenegraph->addChild(transform);

    // calculate vertices
    const int numPoints = 2;
    auto vertices = vsg::vec3Array::create(numPoints);
    auto colors = vsg::vec3Array::create(numPoints);
    double length = 1;
    vsg::vec3 p1(0, length / 2, 0);
    vsg::vec3 p2(0, -length / 2, 0);
    vertices->set(0, p2);
    vertices->set(1, p1);
    auto cv = vsg::vec3(material->GetDiffuseColor().R, material->GetDiffuseColor().G, material->GetDiffuseColor().B);
    colors->set(0, cv);
    colors->set(1, cv);
    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors}));
    drawCommands->addChild(vsg::Draw::create(vertices->size(), 1, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);
    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createPathShape(std::shared_ptr<ChPhysicsItem> physItem,
                                                       ChVisualModel::ShapeInstance shapeInstance,
                                                       std::shared_ptr<ChVisualMaterial> material,
                                                       vsg::ref_ptr<vsg::MatrixTransform> transform,
                                                       std::shared_ptr<ChPathShape> ps) {
    auto scenegraph = vsg::Group::create();
    // store some information for easier update
    scenegraph->setValue("ItemPtr", physItem);
    scenegraph->setValue("ShapeInstancePtr", shapeInstance);
    scenegraph->setValue("TransformPtr", transform);

    vsg::ref_ptr<vsg::ShaderStage> vertexShader = lineShader_vert();
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = lineShader_frag();

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr}  // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}  // projection view, and model matrices, actual push constant calls
                                              // autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},  // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}   // colour data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},  // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}   // colour data
    };

    vsg::ref_ptr<vsg::InputAssemblyState> iaState = vsg::InputAssemblyState::create();
    iaState->topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

    vsg::ref_ptr<vsg::RasterizationState> raState = vsg::RasterizationState::create();
    raState->lineWidth = 1.0;  // only allowed value (also set as standard)

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        iaState,
        raState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto pipelineLayout =
        vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline =
        vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    scenegraph->addChild(bindGraphicsPipeline);

    // set up model transformation node
    // auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    scenegraph->addChild(transform);

    // calculate vertices
    int numPoints = ps->GetNumRenderPoints();
    assert(numPoints > 2);
    double maxU = ps->GetPathGeometry()->GetPathDuration();
    double ustep = maxU / double(numPoints - 1);
    auto vertices = vsg::vec3Array::create(numPoints);
    auto colors = vsg::vec3Array::create(numPoints);
    for (int i = 0; i < numPoints; i++) {
        double u = ustep * (double(i));
        ChVector<> pos;
        ps->GetPathGeometry()->Evaluate(pos, u);
        vertices->set(i, vsg::vec3(pos.x(), pos.y(), pos.z()));
        auto cv =
            vsg::vec3(material->GetDiffuseColor().R, material->GetDiffuseColor().G, material->GetDiffuseColor().B);
        colors->set(i, cv);
    }
    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors}));
    drawCommands->addChild(vsg::Draw::create(vertices->size(), 1, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);
    return scenegraph;
}

vsg::ref_ptr<vsg::Group> ShapeBuilder::createDecoGrid(double ustep,
                                                      double vstep,
                                                      int nu,
                                                      int nv,
                                                      ChCoordsys<> pos,
                                                      ChColor col) {
    auto scenegraph = vsg::Group::create();
    vsg::ref_ptr<vsg::ShaderStage> vertexShader = lineShader_vert();
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = lineShader_frag();

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr}  // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}  // projection view, and model matrices, actual push constant calls
                                              // automatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},  // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}   // colour data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},  // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}   // colour data
    };

    vsg::ref_ptr<vsg::InputAssemblyState> iaState = vsg::InputAssemblyState::create();
    iaState->topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    vsg::ref_ptr<vsg::RasterizationState> raState = vsg::RasterizationState::create();
    raState->lineWidth = 1.0;  // only allowed value (also set as standard)

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        iaState,
        raState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto pipelineLayout =
        vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline =
        vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors
    // to decorate the whole graph
    scenegraph->addChild(bindGraphicsPipeline);

    // set up model transformation node
    // auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    auto transform = vsg::MatrixTransform::create();
    auto p = pos.pos;
    auto r = pos.rot;
    double rotAngle;
    ChVector<> rotAxis;
    r.Q_to_AngAxis(rotAngle, rotAxis);
    transform->matrix =
        vsg::translate(p.x(), p.y(), p.z()) * vsg::rotate(rotAngle, rotAxis.x(), rotAxis.y(), rotAxis.z());

    scenegraph->addChild(transform);
    // calculate vertices
    std::vector<ChVector<>> v;
    for (int iu = -nu / 2; iu <= nu / 2; iu++) {
        ChVector<> V1(iu * ustep, vstep * (nv / 2), 0);
        ChVector<> V2(iu * ustep, -vstep * (nv / 2), 0);
        v.push_back(V1);
        v.push_back(V2);
        // drawSegment(vis, pos.TransformLocalToParent(V1), pos.TransformLocalToParent(V2), col, use_Zbuffer);
    }

    for (int iv = -nv / 2; iv <= nv / 2; iv++) {
        ChVector<> V1(ustep * (nu / 2), iv * vstep, 0);
        ChVector<> V2(-ustep * (nu / 2), iv * vstep, 0);
        v.push_back(V1);
        v.push_back(V2);
        // drawSegment(vis, pos.TransformLocalToParent(V1), pos.TransformLocalToParent(V2), col, use_Zbuffer);
    }

    const int numPoints = v.size();
    auto vertices = vsg::vec3Array::create(numPoints);
    auto colors = vsg::vec3Array::create(numPoints);
    auto cv = vsg::vec3(col.R, col.G, col.B);
    colors->set(0, cv);
    for (size_t i = 0; i < numPoints; i++) {
        vertices->set(i, vsg::vec3(v[i].x(), v[i].y(), v[i].z()));
        colors->set(i, cv);
    }
    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors}));
    drawCommands->addChild(vsg::Draw::create(vertices->size(), 1, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    if (compileTraversal)
        compileTraversal->compile(scenegraph);
    return scenegraph;
}

/// create a ShaderSet for Phong shaded rendering with tiled textures
vsg::ref_ptr<vsg::ShaderSet> ShapeBuilder::createTilingPhongShaderSet(vsg::ref_ptr<const vsg::Options> options) {
    if (options) {
        // check if a ShaderSet has already been assigned to the options object, if so return it
        if (auto itr = options->shaderSets.find("phong"); itr != options->shaderSets.end())
            return itr->second;
    }

    auto vertexShader = vsg::read_cast<vsg::ShaderStage>("vsg/shaders/vsg3d.vert", options);
    // if (!vertexShader)
    //     vertexShader = assimp_vert();  // fallback to shaders/assimp_vert.cpp
    auto fragmentShader = vsg::read_cast<vsg::ShaderStage>("vsg/shaders/vsg3d_phong.frag", options);
    // if (!fragmentShader)
    //     fragmentShader = assimp_phong_frag();

    auto shaderSet = vsg::ShaderSet::create(vsg::ShaderStages{vertexShader, fragmentShader});

    shaderSet->addAttributeBinding("vsg_Vertex", "", 0, VK_FORMAT_R32G32B32_SFLOAT, vsg::vec3Array::create(1));
    shaderSet->addAttributeBinding("vsg_Normal", "", 1, VK_FORMAT_R32G32B32_SFLOAT, vsg::vec3Array::create(1));
    shaderSet->addAttributeBinding("vsg_TexCoord0", "", 2, VK_FORMAT_R32G32_SFLOAT, vsg::vec2Array::create(1));
    shaderSet->addAttributeBinding("vsg_Color", "", 3, VK_FORMAT_R32G32B32A32_SFLOAT, vsg::vec4Array::create(1));
    shaderSet->addAttributeBinding("vsg_position", "VSG_INSTANCE_POSITIONS", 4, VK_FORMAT_R32G32B32_SFLOAT,
                                   vsg::vec3Array::create(1));

    shaderSet->addUniformBinding("displacementMap", "VSG_DISPLACEMENT_MAP", 0, 6,
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                                 vsg::vec4Array2D::create(1, 1));
    shaderSet->addUniformBinding("diffuseMap", "VSG_DIFFUSE_MAP", 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, vsg::vec4Array2D::create(1, 1));
    shaderSet->addUniformBinding("normalMap", "VSG_NORMAL_MAP", 0, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, vsg::vec3Array2D::create(1, 1));
    shaderSet->addUniformBinding("aoMap", "VSG_LIGHTMAP_MAP", 0, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, vsg::vec4Array2D::create(1, 1));
    shaderSet->addUniformBinding("emissiveMap", "VSG_EMISSIVE_MAP", 0, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, vsg::vec4Array2D::create(1, 1));
    shaderSet->addUniformBinding("texrepeat", "", 0, 9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, vsg::vec3Value::create());
    shaderSet->addUniformBinding("material", "", 0, 10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, vsg::PhongMaterialValue::create());
    shaderSet->addUniformBinding("lightData", "", 1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, vsg::vec4Array::create(64));

    shaderSet->addPushConstantRange("pc", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);

    shaderSet->optionalDefines = {"VSG_GREYSACLE_DIFFUSE_MAP", "VSG_TWO_SIDED_LIGHTING", "VSG_POINT_SPRITE"};

    shaderSet->definesArrayStates.push_back(vsg::DefinesArrayState{
        {"VSG_INSTANCE_POSITIONS", "VSG_DISPLACEMENT_MAP"}, vsg::PositionAndDisplacementMapArrayState::create()});
    shaderSet->definesArrayStates.push_back(
        vsg::DefinesArrayState{{"VSG_INSTANCE_POSITIONS"}, vsg::PositionArrayState::create()});
    shaderSet->definesArrayStates.push_back(
        vsg::DefinesArrayState{{"VSG_DISPLACEMENT_MAP"}, vsg::DisplacementMapArrayState::create()});

    return shaderSet;
}

}  // namespace vsg3d
}  // namespace chrono
