/**
 * @file fields.h
 * @author yui
 */


#ifndef FIELDS_H
#define FIELDS_H
namespace chat_sdk
{
    namespace json_fields
    {
        constexpr const char *MODEL = "model";
        constexpr const char *MESSAGES = "messages";
        constexpr const char *MESSAGE = "message";
        constexpr const char *ROLE = "role";
        constexpr const char *CONTENT = "content";
        constexpr const char *CHOICES = "choices";
        constexpr const char *DELTA = "delta";
        constexpr const char *STREAM = "stream";
        constexpr const char *TEMPERATURE = "temperature"; 
        constexpr const char *MAX_TOKENS = "max_tokens"; 
    }
}
#endif