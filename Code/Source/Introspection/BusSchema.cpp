/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "Introspection/BusSchema.h"

#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/sort.h>

#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

namespace AiCompanion
{
    namespace
    {
        using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer>;

        //! Write an AZStd::string as a JSON string with an explicit length so
        //! embedded characters are handled correctly.
        void WriteString(JsonWriter& writer, const AZStd::string& value)
        {
            writer.String(value.c_str(), static_cast<rapidjson::SizeType>(value.size()));
        }

        //! Emit a single-key {"error": message} object.
        AZStd::string ErrorObject(rapidjson::StringBuffer& buffer, JsonWriter& writer, const AZStd::string& message)
        {
            writer.StartObject();
            writer.Key("error");
            WriteString(writer, message);
            writer.EndObject();
            return AZStd::string(buffer.GetString(), buffer.GetSize());
        }

        //! Write the argument array for one event method, skipping the leading
        //! EBus address argument (present when the bus is addressed by id) and
        //! any implicit this pointer. GetArgument and GetArgumentName share an
        //! index, so types and names stay paired once those are excluded.
        void WriteArgs(JsonWriter& writer, const AZ::BehaviorMethod& method)
        {
            writer.Key("args");
            writer.StartArray();
            const size_t numArguments = method.GetNumArguments();
            for (size_t index = 0; index < numArguments; ++index)
            {
                const AZ::BehaviorParameter* parameter = method.GetArgument(index);
                if (parameter == nullptr)
                {
                    continue;
                }
                // For an EBus addressed by id, argument 0 is the bus id, not a
                // user argument; the .pyi stub excludes it too.
                if (method.HasBusId() && index == 0)
                {
                    continue;
                }
                if ((parameter->m_traits & AZ::BehaviorParameter::TR_THIS_PTR) != 0)
                {
                    continue;
                }

                writer.StartObject();

                writer.Key("name");
                const AZStd::string* argumentName = method.GetArgumentName(index);
                if (argumentName != nullptr && !argumentName->empty())
                {
                    WriteString(writer, *argumentName);
                }
                else
                {
                    WriteString(writer, AZStd::string::format("arg%zu", index));
                }

                writer.Key("type");
                writer.String(parameter->m_name != nullptr ? parameter->m_name : "");

                writer.Key("tooltip");
                const AZStd::string* toolTip = method.GetArgumentToolTip(index);
                WriteString(writer, (toolTip != nullptr) ? *toolTip : AZStd::string());

                writer.EndObject();
            }
            writer.EndArray();
        }

        //! Write the full {name, events:[...]} object for one bus.
        void WriteBus(JsonWriter& writer, const AZ::BehaviorEBus& ebus)
        {
            AZStd::vector<AZStd::string> eventNames;
            eventNames.reserve(ebus.m_events.size());
            for (const auto& entry : ebus.m_events)
            {
                eventNames.push_back(entry.first);
            }
            AZStd::sort(eventNames.begin(), eventNames.end());

            writer.StartObject();
            writer.Key("name");
            WriteString(writer, ebus.m_name);
            writer.Key("events");
            writer.StartArray();

            for (const AZStd::string& eventName : eventNames)
            {
                // eventName came from m_events just above, so the lookup always hits.
                const AZ::BehaviorEBusEventSender& sender = ebus.m_events.find(eventName)->second;
                // Prefer the addressable Event method; fall back to Broadcast (used
                // by buses with no address, e.g. AddressPolicy::Single).
                const AZ::BehaviorMethod* method = sender.m_event != nullptr ? sender.m_event : sender.m_broadcast;
                if (method == nullptr)
                {
                    continue;
                }

                writer.StartObject();
                writer.Key("name");
                WriteString(writer, eventName);
                writer.Key("call_type");
                writer.String(sender.m_event != nullptr ? "Event" : "Broadcast");

                WriteArgs(writer, *method);

                writer.Key("returns");
                const AZ::BehaviorParameter* result = method->GetResult();
                writer.String((result != nullptr && result->m_name != nullptr) ? result->m_name : "void");

                writer.EndObject();
            }

            writer.EndArray();
            writer.EndObject();
        }
    } // namespace

    AZStd::string BuildBusSchemaJson(AZ::BehaviorContext* behaviorContext, AZStd::string_view busName)
    {
        rapidjson::StringBuffer buffer;
        JsonWriter writer(buffer);

        if (behaviorContext == nullptr)
        {
            return ErrorObject(buffer, writer, "BehaviorContext is not available");
        }

        // Empty bus name: list every reflected bus, sorted.
        if (busName.empty())
        {
            AZStd::vector<AZStd::string> names;
            names.reserve(behaviorContext->m_ebuses.size());
            for (const auto& entry : behaviorContext->m_ebuses)
            {
                names.push_back(entry.first);
            }
            AZStd::sort(names.begin(), names.end());

            writer.StartObject();
            writer.Key("buses");
            writer.StartArray();
            for (const AZStd::string& name : names)
            {
                WriteString(writer, name);
            }
            writer.EndArray();
            writer.EndObject();
            return AZStd::string(buffer.GetString(), buffer.GetSize());
        }

        const auto busIterator = behaviorContext->m_ebuses.find(AZStd::string(busName));
        if (busIterator == behaviorContext->m_ebuses.end() || busIterator->second == nullptr)
        {
            return ErrorObject(
                buffer, writer, AZStd::string::format("No reflected bus named '%.*s'", AZ_STRING_ARG(busName)));
        }

        WriteBus(writer, *busIterator->second);
        return AZStd::string(buffer.GetString(), buffer.GetSize());
    }
} // namespace AiCompanion
