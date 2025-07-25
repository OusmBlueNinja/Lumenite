-- app/middleware.lua

app.after_request(function(request, response)
    response.headers["X-Powered-By"] = "Lumenite"
    return response
end)
