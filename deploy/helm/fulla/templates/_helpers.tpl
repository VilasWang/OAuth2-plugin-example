{{/* Chart name */}}
{{- define "fulla.name" -}}
{{- .Chart.Name | trunc 63 | trimSuffix "-" -}}
{{- end }}

{{/* Fully qualified release name */}}
{{- define "fulla.fullname" -}}
{{- if contains .Chart.Name .Release.Name -}}
{{- .Release.Name | trunc 63 | trimSuffix "-" -}}
{{- else -}}
{{- printf "%s-%s" .Release.Name .Chart.Name | trunc 63 | trimSuffix "-" -}}
{{- end -}}
{{- end }}

{{/* Common labels */}}
{{- define "fulla.labels" -}}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
app.kubernetes.io/name: {{ include "fulla.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/* Image tag (defaults to appVersion) */}}
{{- define "fulla.imageTag" -}}
{{- default .Chart.AppVersion .Values.image.tag -}}
{{- end }}

{{- define "fulla.backendImage" -}}
{{- printf "%s/%s-backend:%s" .Values.image.registry .Values.image.prefix (include "fulla.imageTag" .) -}}
{{- end }}

{{- define "fulla.frontendImage" -}}
{{- printf "%s/%s-frontend:%s" .Values.image.registry .Values.image.prefix (include "fulla.imageTag" .) -}}
{{- end }}

{{- define "fulla.adminImage" -}}
{{- printf "%s/%s-admin:%s" .Values.image.registry .Values.image.prefix (include "fulla.imageTag" .) -}}
{{- end }}

{{/* Effective DB / Redis endpoints (in-chart service or external host) */}}
{{- define "fulla.dbHost" -}}
{{- if .Values.postgresql.enabled -}}
{{- printf "%s-postgresql" (include "fulla.fullname" .) -}}
{{- else -}}
{{- required "externalDatabase.host is required when postgresql.enabled=false" .Values.externalDatabase.host -}}
{{- end -}}
{{- end }}

{{- define "fulla.redisHost" -}}
{{- if .Values.redis.enabled -}}
{{- printf "%s-redis" (include "fulla.fullname" .) -}}
{{- else -}}
{{- required "externalRedis.host is required when redis.enabled=false" .Values.externalRedis.host -}}
{{- end -}}
{{- end }}

{{/* Name of the Secret consumed via envFrom */}}
{{- define "fulla.secretName" -}}
{{- default (printf "%s-secrets" (include "fulla.fullname" .)) .Values.secrets.existingSecret -}}
{{- end }}

{{/* Shared envFrom block for backend + migration Job */}}
{{- define "fulla.backendEnvFrom" -}}
envFrom:
  - configMapRef:
      name: {{ include "fulla.fullname" . }}-config
  - secretRef:
      name: {{ include "fulla.secretName" . }}
{{- end }}

{{/*
Hook events for the migration Job and its config/secret copies. With the
in-chart postgres the database does not exist yet during pre-install hooks
(regular resources are only created afterwards), so the first-install
migration runs post-install instead; upgrades always migrate pre-upgrade.
With an external database the Job is a true pre-install gate.
*/}}
{{- define "fulla.migrationHookEvents" -}}
{{- if .Values.postgresql.enabled -}}post-install,pre-upgrade{{- else -}}pre-install,pre-upgrade{{- end -}}
{{- end }}

{{/*
Backend environment (non-secret), shared verbatim between the regular
ConfigMap and the migration hook's copy. Mirrors the fulla-backend env
block of deploy/docker/docker-compose.prod.yml.
*/}}
{{- define "fulla.backendConfigData" -}}
FULLA_ENV: {{ .Values.backend.env | quote }}
FULLA_ISSUER: {{ .Values.backend.issuer | quote }}
{{- if .Values.backend.jwtKey.existingSecret }}
FULLA_JWT_KEY_PATH: {{ printf "/app/keys/%s" .Values.backend.jwtKey.keyName | quote }}
{{- else }}
FULLA_JWT_KEY_PATH: ""
{{- end }}
FULLA_DB_HOST: {{ include "fulla.dbHost" . | quote }}
FULLA_DB_PORT: {{ (.Values.postgresql.enabled | ternary 5432 .Values.externalDatabase.port) | toString | quote }}
FULLA_DB_NAME: {{ .Values.externalDatabase.name | quote }}
FULLA_DB_USER: {{ .Values.externalDatabase.user | quote }}
FULLA_REDIS_HOST: {{ include "fulla.redisHost" . | quote }}
FULLA_REDIS_PORT: {{ (.Values.redis.enabled | ternary 6379 .Values.externalRedis.port) | toString | quote }}
FULLA_LISTEN_PORT: {{ .Values.backend.listenPort | toString | quote }}
FULLA_FRONTEND_URL: {{ .Values.backend.frontendUrl | quote }}
FULLA_CORS_ALLOW_ORIGINS: {{ .Values.backend.corsAllowOrigins | quote }}
FULLA_VUE_REDIRECT_URI: {{ .Values.backend.vueRedirectUri | quote }}
FULLA_GOOGLE_REDIRECT_URI: {{ .Values.backend.googleRedirectUri | quote }}
FULLA_AUTO_MIGRATE: {{ .Values.backend.autoMigrate | toString | quote }}
DETAILED_VALIDATION_ERRORS: {{ .Values.backend.detailedValidationErrors | quote }}
FULLA_GITHUB_CLIENT_ID: {{ .Values.backend.githubClientId | quote }}
FULLA_GOOGLE_CLIENT_ID: {{ .Values.backend.googleClientId | quote }}
FULLA_SMTP_HOST: {{ .Values.backend.smtp.host | quote }}
FULLA_SMTP_PORT: {{ .Values.backend.smtp.port | toString | quote }}
FULLA_SMTP_USER: {{ .Values.backend.smtp.user | quote }}
FULLA_SMTP_FROM_NAME: {{ .Values.backend.smtp.fromName | quote }}
FULLA_SMTP_SSL: {{ .Values.backend.smtp.ssl | quote }}
{{- end }}

{{/*
Backend secret env (stringData), shared between the regular Secret and the
migration hook's copy. Only rendered when secrets.existingSecret is unset.
*/}}
{{- define "fulla.backendSecretData" -}}
FULLA_DB_PASSWORD: {{ required "secrets.dbPassword is required (or set secrets.existingSecret)" .Values.secrets.dbPassword | quote }}
FULLA_REDIS_PASSWORD: {{ required "secrets.redisPassword is required (or set secrets.existingSecret)" .Values.secrets.redisPassword | quote }}
FULLA_VUE_CLIENT_SECRET: {{ required "secrets.vueClientSecret is required (or set secrets.existingSecret)" .Values.secrets.vueClientSecret | quote }}
FULLA_GITHUB_CLIENT_SECRET: {{ .Values.secrets.githubClientSecret | quote }}
FULLA_GOOGLE_CLIENT_SECRET: {{ .Values.secrets.googleClientSecret | quote }}
FULLA_SMTP_PASSWORD: {{ .Values.secrets.smtpPassword | quote }}
{{- end }}
