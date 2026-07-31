{{/* Chart name */}}
{{- define "authforge.name" -}}
{{- .Chart.Name | trunc 63 | trimSuffix "-" -}}
{{- end }}

{{/* Fully qualified release name */}}
{{- define "authforge.fullname" -}}
{{- if contains .Chart.Name .Release.Name -}}
{{- .Release.Name | trunc 63 | trimSuffix "-" -}}
{{- else -}}
{{- printf "%s-%s" .Release.Name .Chart.Name | trunc 63 | trimSuffix "-" -}}
{{- end -}}
{{- end }}

{{/* Common labels */}}
{{- define "authforge.labels" -}}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
app.kubernetes.io/name: {{ include "authforge.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/* Image tag (defaults to appVersion) */}}
{{- define "authforge.imageTag" -}}
{{- default .Chart.AppVersion .Values.image.tag -}}
{{- end }}

{{- define "authforge.backendImage" -}}
{{- printf "%s/%s-backend:%s" .Values.image.registry .Values.image.prefix (include "authforge.imageTag" .) -}}
{{- end }}

{{- define "authforge.frontendImage" -}}
{{- printf "%s/%s-frontend:%s" .Values.image.registry .Values.image.prefix (include "authforge.imageTag" .) -}}
{{- end }}

{{- define "authforge.adminImage" -}}
{{- printf "%s/%s-admin:%s" .Values.image.registry .Values.image.prefix (include "authforge.imageTag" .) -}}
{{- end }}

{{/* Effective DB / Redis endpoints (in-chart service or external host) */}}
{{- define "authforge.dbHost" -}}
{{- if .Values.postgresql.enabled -}}
{{- printf "%s-postgresql" (include "authforge.fullname" .) -}}
{{- else -}}
{{- required "externalDatabase.host is required when postgresql.enabled=false" .Values.externalDatabase.host -}}
{{- end -}}
{{- end }}

{{- define "authforge.redisHost" -}}
{{- if .Values.redis.enabled -}}
{{- printf "%s-redis" (include "authforge.fullname" .) -}}
{{- else -}}
{{- required "externalRedis.host is required when redis.enabled=false" .Values.externalRedis.host -}}
{{- end -}}
{{- end }}

{{/* Name of the Secret consumed via envFrom */}}
{{- define "authforge.secretName" -}}
{{- default (printf "%s-secrets" (include "authforge.fullname" .)) .Values.secrets.existingSecret -}}
{{- end }}

{{/* Shared envFrom block for backend + migration Job */}}
{{- define "authforge.backendEnvFrom" -}}
envFrom:
  - configMapRef:
      name: {{ include "authforge.fullname" . }}-config
  - secretRef:
      name: {{ include "authforge.secretName" . }}
{{- end }}

{{/*
Hook events for the migration Job and its config/secret copies. With the
in-chart postgres the database does not exist yet during pre-install hooks
(regular resources are only created afterwards), so the first-install
migration runs post-install instead; upgrades always migrate pre-upgrade.
With an external database the Job is a true pre-install gate.
*/}}
{{- define "authforge.migrationHookEvents" -}}
{{- if .Values.postgresql.enabled -}}post-install,pre-upgrade{{- else -}}pre-install,pre-upgrade{{- end -}}
{{- end }}

{{/*
Backend environment (non-secret), shared verbatim between the regular
ConfigMap and the migration hook's copy. Mirrors the oauth2-backend env
block of deploy/docker/docker-compose.prod.yml.
*/}}
{{- define "authforge.backendConfigData" -}}
OAUTH2_ENV: {{ .Values.backend.env | quote }}
OAUTH2_ISSUER: {{ .Values.backend.issuer | quote }}
{{- if .Values.backend.jwtKey.existingSecret }}
OAUTH2_JWT_KEY_PATH: {{ printf "/app/keys/%s" .Values.backend.jwtKey.keyName | quote }}
{{- else }}
OAUTH2_JWT_KEY_PATH: ""
{{- end }}
OAUTH2_DB_HOST: {{ include "authforge.dbHost" . | quote }}
OAUTH2_DB_PORT: {{ (.Values.postgresql.enabled | ternary 5432 .Values.externalDatabase.port) | toString | quote }}
OAUTH2_DB_NAME: {{ .Values.externalDatabase.name | quote }}
OAUTH2_DB_USER: {{ .Values.externalDatabase.user | quote }}
OAUTH2_REDIS_HOST: {{ include "authforge.redisHost" . | quote }}
OAUTH2_REDIS_PORT: {{ (.Values.redis.enabled | ternary 6379 .Values.externalRedis.port) | toString | quote }}
OAUTH2_LISTEN_PORT: {{ .Values.backend.listenPort | toString | quote }}
OAUTH2_FRONTEND_URL: {{ .Values.backend.frontendUrl | quote }}
OAUTH2_CORS_ALLOW_ORIGINS: {{ .Values.backend.corsAllowOrigins | quote }}
OAUTH2_VUE_REDIRECT_URI: {{ .Values.backend.vueRedirectUri | quote }}
OAUTH2_GOOGLE_REDIRECT_URI: {{ .Values.backend.googleRedirectUri | quote }}
OAUTH2_AUTO_MIGRATE: {{ .Values.backend.autoMigrate | toString | quote }}
DETAILED_VALIDATION_ERRORS: {{ .Values.backend.detailedValidationErrors | quote }}
OAUTH2_GITHUB_CLIENT_ID: {{ .Values.backend.githubClientId | quote }}
OAUTH2_GOOGLE_CLIENT_ID: {{ .Values.backend.googleClientId | quote }}
OAUTH2_SMTP_HOST: {{ .Values.backend.smtp.host | quote }}
OAUTH2_SMTP_PORT: {{ .Values.backend.smtp.port | toString | quote }}
OAUTH2_SMTP_USER: {{ .Values.backend.smtp.user | quote }}
OAUTH2_SMTP_FROM_NAME: {{ .Values.backend.smtp.fromName | quote }}
OAUTH2_SMTP_SSL: {{ .Values.backend.smtp.ssl | quote }}
{{- end }}

{{/*
Backend secret env (stringData), shared between the regular Secret and the
migration hook's copy. Only rendered when secrets.existingSecret is unset.
*/}}
{{- define "authforge.backendSecretData" -}}
OAUTH2_DB_PASSWORD: {{ required "secrets.dbPassword is required (or set secrets.existingSecret)" .Values.secrets.dbPassword | quote }}
OAUTH2_REDIS_PASSWORD: {{ required "secrets.redisPassword is required (or set secrets.existingSecret)" .Values.secrets.redisPassword | quote }}
OAUTH2_VUE_CLIENT_SECRET: {{ required "secrets.vueClientSecret is required (or set secrets.existingSecret)" .Values.secrets.vueClientSecret | quote }}
OAUTH2_GITHUB_CLIENT_SECRET: {{ .Values.secrets.githubClientSecret | quote }}
OAUTH2_GOOGLE_CLIENT_SECRET: {{ .Values.secrets.googleClientSecret | quote }}
OAUTH2_SMTP_PASSWORD: {{ .Values.secrets.smtpPassword | quote }}
{{- end }}
