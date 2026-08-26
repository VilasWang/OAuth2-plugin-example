window.onload = function() {
  //<editor-fold desc="Changeable Configuration Block">

  // OAuth2 API Documentation
  window.ui = SwaggerUIBundle({
    url: "/docs/api/openapi.json",
    dom_id: '#swagger-ui',
    deepLinking: true,
    presets: [
      SwaggerUIBundle.presets.apis,
      SwaggerUIStandalonePreset
    ],
    plugins: [
      SwaggerUIBundle.plugins.DownloadUrl
    ],
    layout: "StandaloneLayout",
    defaultModelsExpandDepth: 1,
    defaultModelExpandDepth: 1,
    docExpansion: "list",
    filter: true,
    showRequestHeaders: true
  });

  //</editor-fold>
};
