#pragma once

#define SEND_ERROR(CALLBACK, STATUS_CODE, BODY)                                                         \
    {                                                                                                   \
        HttpResponsePtr response = HttpResponse::newHttpResponse();                                     \
        response->setStatusCode(STATUS_CODE);                                                           \
        response->setBody(BODY);                                                                        \
        response->setContentTypeString("text/plain; charset=utf-8");                                    \
        CALLBACK(response);                                                                             \
        return;                                                                                         \
    }
