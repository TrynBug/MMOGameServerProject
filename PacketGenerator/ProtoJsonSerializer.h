#pragma once

#include "pch.h"

namespace packet
{

// protobuf message를 json string으로 변환기능 제공 클래스
class ProtoJsonSerializer
{
public:
    // protobuf message를 json string으로 변환
    static bool ToJson(const google::protobuf::Message& msg, std::string& outJson, bool prettyPrint = false)
    {
        google::protobuf::json::PrintOptions options;
        options.add_whitespace = prettyPrint;                // 멤버 변수명 변경 적용
		options.always_print_fields_with_no_presence = true; // 0값, 빈 리스트/맵도 출력
        options.preserve_proto_field_names = true;           // snake_case 유지

        auto status = google::protobuf::json::MessageToJsonString(msg, &outJson, options);
        return status.ok();
    }

    // json string을 protobuf message로 변환
    static bool FromJson(const std::string& json, google::protobuf::Message& outMsg)
    {
        google::protobuf::json::ParseOptions options;
        auto status = google::protobuf::json::JsonStringToMessage(json, &outMsg, options);
        return status.ok();
    }
};

} // namespace packet
