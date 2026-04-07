/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "SceneSnapshotProvider.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/sort.h>
#include <AzCore/JSON/document.h>
#include <AzCore/JSON/writer.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/prettywriter.h>

namespace AiCompanion
{
    namespace Internal
    {
        struct EntityInfo
        {
            AZ::EntityId id;
            AZStd::string name;
            AZ::EntityId parentId;
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            AZ::Vector3 rotation = AZ::Vector3::CreateZero();
            AZ::Vector3 scale = AZ::Vector3::CreateOne();
            AZStd::vector<AZStd::string> componentNames;
        };

        static AZStd::vector<EntityInfo> GatherEntityInfos()
        {
            AZStd::vector<EntityInfo> infos;

            AZ::ComponentApplicationBus::Broadcast(
                [&infos](AZ::ComponentApplicationRequests* appRequests)
                {
                    appRequests->EnumerateEntities(
                        [&infos](AZ::Entity* entity)
                        {
                            if (!entity)
                            {
                                return true;
                            }

                            EntityInfo info;
                            info.id = entity->GetId();
                            info.name = entity->GetName();

                            // Get transform data
                            AZ::TransformBus::EventResult(
                                info.position, info.id, &AZ::TransformBus::Events::GetWorldTranslation);

                            AZ::Quaternion quat = AZ::Quaternion::CreateIdentity();
                            AZ::TransformBus::EventResult(
                                quat, info.id, &AZ::TransformBus::Events::GetWorldRotationQuaternion);
                            info.rotation = quat.GetEulerDegrees();

                            AZ::TransformBus::EventResult(
                                info.scale, info.id, &AZ::TransformBus::Events::GetLocalScale);

                            // Get parent
                            AZ::TransformBus::EventResult(
                                info.parentId, info.id, &AZ::TransformBus::Events::GetParentId);

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

                            infos.push_back(AZStd::move(info));
                            return true; // continue enumeration
                        });
                });

            return infos;
        }

        static void WriteEntityJson(
            rapidjson::Writer<rapidjson::StringBuffer>& writer,
            const EntityInfo& info)
        {
            writer.StartObject();

            writer.Key("id");
            writer.Uint64(static_cast<AZ::u64>(info.id));

            writer.Key("name");
            writer.String(info.name.c_str(), static_cast<rapidjson::SizeType>(info.name.size()));

            writer.Key("parent_id");
            if (info.parentId.IsValid())
            {
                writer.Uint64(static_cast<AZ::u64>(info.parentId));
            }
            else
            {
                writer.Null();
            }

            writer.Key("position");
            writer.StartArray();
            writer.Double(static_cast<double>(info.position.GetX()));
            writer.Double(static_cast<double>(info.position.GetY()));
            writer.Double(static_cast<double>(info.position.GetZ()));
            writer.EndArray();

            writer.Key("rotation");
            writer.StartArray();
            writer.Double(static_cast<double>(info.rotation.GetX()));
            writer.Double(static_cast<double>(info.rotation.GetY()));
            writer.Double(static_cast<double>(info.rotation.GetZ()));
            writer.EndArray();

            writer.Key("scale");
            writer.StartArray();
            writer.Double(static_cast<double>(info.scale.GetX()));
            writer.Double(static_cast<double>(info.scale.GetY()));
            writer.Double(static_cast<double>(info.scale.GetZ()));
            writer.EndArray();

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
            writer.Uint64(static_cast<AZ::u64>(info.id));

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
                writer.Uint64(static_cast<AZ::u64>(info.id));
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
                writer.Uint64(static_cast<AZ::u64>(info.id));
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
                writer.Uint64(static_cast<AZ::u64>(info.id));
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
