<script setup lang="ts">
import AppLogo from '../components/shared/AppLogo.vue'

// "Vault door" composition: the issuer line lets users verify the issuing
// party before typing a password. Hidden when no issuer is configured.
const issuer = (import.meta.env.VITE_ISSUER as string | undefined) || ''
</script>

<template>
  <div class="min-h-screen flex flex-col items-center bg-page blueprint-grid">
    <!-- Machined top blade -->
    <div class="fixed inset-x-0 top-0 h-[3px] bg-brand-600 z-10" />

    <main class="flex-1 flex flex-col items-center justify-center w-full px-6 py-12">
      <!-- Brand row -->
      <div class="mb-7">
        <AppLogo size="lg" />
      </div>

      <!-- Auth card -->
      <div class="w-full max-w-[440px] bg-surface border border-neutral-200 rounded-auth shadow-md p-9 pb-[30px]">
        <slot />
      </div>

      <!-- Issuer verification line -->
      <p
        v-if="issuer"
        class="mt-6 font-mono text-xs text-neutral-500 tabular-nums"
      >
        <span class="font-semibold text-neutral-600">issuer</span> &middot; {{ issuer }}
      </p>

      <!-- Legal line -->
      <p class="mt-2.5 text-xs text-neutral-400">
        Fulla Identity Platform &middot; MFA protected &middot; OIDC conformant
      </p>
    </main>
  </div>
</template>
