/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "SceneSnapshotProvider.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/JSON/document.h>
#include <AzCore/JSON/prettywriter.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/sort.h>

namespace AiCompanion
{
    namespace Internal
    {
        using EntityInfo = SceneSnapshotProvider::EntityInfo;

        static EntityInfo BuildEntityInfo(AZ::Entity* entity)
        {
            EntityInfo info;
            info.id = entity->GetId();
            info.name = entity->GetName();

            // Get transform data
            AZ::TransformBus::EventResult(info.position, info.id, &AZ::TransformBus::Events::GetWorldTranslation);

            AZ::Quaternion quat = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(quat, info.id, &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            info.rotation = quat.GetEulerDegrees();

            AZ::TransformBus::EventResult(info.uniformScale, info.id, &AZ::TransformBus::Events::GetLocalUniformScale);

            // Only an entity with a Non-uniform Scale component (the editor's
            // EditorNonUniformScaleComponent, or AzFramework's at runtime) has
            // a handler on this bus; without one EventResult leaves the
            // default untouched, which would read as a scale of zero.
            if (AZ::NonUniformScaleRequestBus::HasHandlers(info.id))
            {
                AZ::Vector3 nonUniformScale = AZ::Vector3::CreateOne();
                AZ::NonUniformScaleRequestBus::EventResult(nonUniformScale, info.id, &AZ::NonUniformScaleRequestBus::Events::GetScale);
                info.nonUniformScale = nonUniformScale;
            }

            // Get parent
            AZ::TransformBus::EventResult(info.parentId, info.id, &AZ::TransformBus::Events::GetParentId);

            // Gather component type names
            const auto& components = entity->GetComponents();
            info.componentNames.reserve(components.size());
            for (const AZ::Component* component : components)
            {
                if (component)
                {
                    const char* name = component->RTTI_GetTypeName();
                    if (name)
                    {
                        info.componentNames.emplace_back(name);
                    }
                }
            }
            return info;
        }

        static AZStd::vector<EntityInfo> GatherEntityInfos()
        {
            AZStd::vector<EntityInfo> infos;

            AZ::ComponentApplicationBus::Broadcast(
                [&infos](AZ::ComponentApplicationRequests* appRequests)
                {
                    appRequests->EnumerateEntities(
                        [&infos](AZ::Entity* entity)
                        {
                            if (entity)
                            {
                                infos.push_back(BuildEntityInfo(entity));
                            }
                            return true; // continue enumeration
                        });
                });

            return infos;
        }

        //! Entity ids are random 64-bit values, mostly above 2^53, which a
        //! double-based JSON parser would corrupt as numbers, so every id in
        //! the native output is a decimal string (API_VERSION 0.4.0 and up).
        static void WriteEntityIdString(rapidjson::Writer<rapidjson::StringBuffer>& writer, AZ::EntityId entityId)
        {
            const AZStd::string text = AZStd::string::format("%llu", static_cast<unsigned long long>(static_cast<AZ::u64>(entityId)));
            writer.String(text.c_str(), static_cast<rapidjson::SizeType>(text.size()));
        }

        static void WriteVector3(rapidjson::Writer<rapidjson::StringBuffer>& writer, const AZ::Vector3& v)
        {
            writer.StartArray();
            writer.Double(static_cast<double>(v.GetX()));
            writer.Double(static_cast<double>(v.GetY()));
            writer.Double(static_cast<double>(v.GetZ()));
            writer.EndArray();
        }

        static void WriteEntityJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const EntityInfo& info)
        {
            writer.StartObject();

            writer.Key("id");
            WriteEntityIdString(writer, info.id);

            writer.Key("name");
            writer.String(info.name.c_str(), static_cast<rapidjson::SizeType>(info.name.size()));

            writer.Key("parent_id");
            if (info.parentId.IsValid())
            {
                WriteEntityIdString(writer, info.parentId);
            }
            else
            {
                writer.Null();
            }

            writer.Key("position");
            WriteVector3(writer, info.position);

            writer.Key("rotation");
            WriteVector3(writer, info.rotation);

            // "scale" has always been the Transform's uniform scale on every
            // axis; the two fields after it carry the Non-uniform Scale
            // component (API_VERSION 0.5.0 and up).
            writer.Key("scale");
            WriteVector3(writer, AZ::Vector3(info.uniformScale));

            writer.Key("non_uniform_scale");
            if (info.nonUniformScale.has_value())
            {
                WriteVector3(writer, *info.nonUniformScale);
            }
            else
            {
                writer.Null();
            }

            writer.Key("effective_scale");
            WriteVector3(writer, SceneSnapshotProvider::EffectiveScale(info.uniformScale, info.nonUniformScale));

            writer.Key("components");
            writer.StartArray();
            for (const auto& comp : info.componentNames)
            {
                writer.String(comp.c_str(), static_cast<rapidjson::SizeType>(comp.size()));
            }
            writer.EndArray();

            writer.EndObject();
        }
    } // namespace Internal

    AZ::Vector3 SceneSnapshotProvider::EffectiveScale(float uniformScale, const AZStd::optional<AZ::Vector3>& nonUniformScale)
    {
        if (nonUniformScale.has_value())
        {
            return *nonUniformScale * uniformScale;
        }
        return AZ::Vector3(uniformScale);
    }

    AZStd::string SceneSnapshotProvider::EntityToJson(const EntityInfo& info)
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        Internal::WriteEntityJson(writer, info);
        return AZStd::string(buffer.GetString(), buffer.GetSize());
    }

    AZStd::string SceneSnapshotProvider::CaptureSnapshot()
    {
        auto infos = Internal::GatherEntityInfos();

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        writer.StartObject();

        writer.Key("entity_count");
        writer.Uint64(infos.size());

        writer.Key("entities");
        writer.StartArray();
        for (const auto& info : infos)
        {
            Internal::WriteEntityJson(writer, info);
        }
        writer.EndArray();

        writer.EndObject();

        return AZStd::string(buffer.GetString(), buffer.GetSize());
    }

    AZStd::string SceneSnapshotProvider::CaptureEntity(AZ::EntityId entityId)
    {
        AZ::Entity* entity = nullptr;
        if (entityId.IsValid())
        {
            AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, entityId);
        }

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        if (!entity)
        {
            writer.StartObject();
            writer.Key("error");
            AZStd::string message =
                AZStd::string::format("No entity with id %llu", static_cast<unsigned long long>(static_cast<AZ::u64>(entityId)));
            writer.String(message.c_str(), static_cast<rapidjson::SizeType>(message.size()));
            writer.Key("entity_id");
            Internal::WriteEntityIdString(writer, entityId);
            writer.EndObject();
            return AZStd::string(buffer.GetString(), buffer.GetSize());
        }

        Internal::WriteEntityJson(writer, Internal::BuildEntityInfo(entity));
        return AZStd::string(buffer.GetString(), buffer.GetSize());
    }

    AZStd::string SceneSnapshotProvider::CaptureEntityTree()
    {
        auto infos = Internal::GatherEntityInfos();

        // Build parent->children map
        AZStd::unordered_map<AZ::u64, AZStd::vector<size_t>> childrenMap;
        AZStd::vector<size_t> roots;

        for (size_t i = 0; i < infos.size(); ++i)
        {
            if (infos[i].parentId.IsValid())
            {
                childrenMap[static_cast<AZ::u64>(infos[i].parentId)].push_back(i);
            }
            else
            {
                roots.push_back(i);
            }
        }

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        // Recursive lambda for tree building
        AZStd::function<void(size_t)> writeNode = [&](size_t idx)
        {
            const auto& info = infos[idx];
            writer.StartObject();

            writer.Key("id");
            Internal::WriteEntityIdString(writer, info.id);

            writer.Key("name");
            writer.String(info.name.c_str(), static_cast<rapidjson::SizeType>(info.name.size()));

            writer.Key("component_count");
            writer.Uint64(info.componentNames.size());

            writer.Key("children");
            writer.StartArray();
            auto it = childrenMap.find(static_cast<AZ::u64>(info.id));
            if (it != childrenMap.end())
            {
                for (size_t childIdx : it->second)
                {
                    writeNode(childIdx);
                }
            }
            writer.EndArray();

            writer.EndObject();
        };

        writer.StartObject();
        writer.Key("roots");
        writer.StartArray();
        for (size_t rootIdx : roots)
        {
            writeNode(rootIdx);
        }
        writer.EndArray();
        writer.EndObject();

        return AZStd::string(buffer.GetString(), buffer.GetSize());
    }

    AZStd::string SceneSnapshotProvider::ValidateScene()
    {
        auto infos = Internal::GatherEntityInfos();

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        writer.StartObject();

        writer.Key("entity_count");
        writer.Uint64(infos.size());

        writer.Key("warnings");
        writer.StartArray();

        for (const auto& info : infos)
        {
            // Check for unnamed entities
            if (info.name.empty())
            {
                writer.StartObject();
                writer.Key("type");
                writer.String("unnamed_entity");
                writer.Key("entity_id");
                Internal::WriteEntityIdString(writer, info.id);
                writer.Key("message");
                writer.String("Entity has no name");
                writer.EndObject();
            }

            // Check for entities with no components (besides TransformComponent)
            if (info.componentNames.size() <= 1)
            {
                writer.StartObject();
                writer.Key("type");
                writer.String("minimal_entity");
                writer.Key("entity_id");
                Internal::WriteEntityIdString(writer, info.id);
                writer.Key("entity_name");
                writer.String(info.name.c_str(), static_cast<rapidjson::SizeType>(info.name.size()));
                writer.Key("message");
                writer.String("Entity has no components beyond TransformComponent");
                writer.EndObject();
            }

            // Check for entities stacked at origin
            const float epsilon = 0.001f;
            if (info.position.GetLength() < epsilon && !info.parentId.IsValid())
            {
                writer.StartObject();
                writer.Key("type");
                writer.String("at_origin");
                writer.Key("entity_id");
                Internal::WriteEntityIdString(writer, info.id);
                writer.Key("entity_name");
                writer.String(info.name.c_str(), static_cast<rapidjson::SizeType>(info.name.size()));
                writer.Key("message");
                writer.String("Root entity is positioned at the origin");
                writer.EndObject();
            }
        }

        writer.EndArray();

        writer.Key("status");
        writer.String("ok");

        writer.EndObject();

        return AZStd::string(buffer.GetString(), buffer.GetSize());
    }
} // namespace AiCompanion
