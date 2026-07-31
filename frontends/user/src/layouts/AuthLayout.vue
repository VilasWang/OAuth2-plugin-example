<script setup lang="ts">
import AppLogo from '../components/shared/AppLogo.vue'
</script>

<template>
  <div class="min-h-screen flex">
    <!-- Left Panel: Brand -->
    <div class="hidden lg:flex lg:w-1/2 bg-gradient-to-br from-indigo-600 via-indigo-700 to-purple-800 relative overflow-hidden">
      <div class="absolute inset-0 opacity-10">
        <svg class="w-full h-full" viewBox="0 0 400 400" fill="none">
          <circle cx="200" cy="200" r="150" stroke="white" stroke-width="0.5" />
          <circle cx="200" cy="200" r="100" stroke="white" stroke-width="0.5" />
          <circle cx="200" cy="200" r="50" stroke="white" stroke-width="0.5" />
          <line x1="0" y1="200" x2="400" y2="200" stroke="white" stroke-width="0.3" />
          <line x1="200" y1="0" x2="200" y2="400" stroke="white" stroke-width="0.3" />
        </svg>
      </div>
      <div class="relative z-10 flex flex-col justify-center px-12 text-white">
        <div class="mb-8">
          <div class="w-12 h-12 rounded-xl bg-white/20 backdrop-blur-sm flex items-center justify-center text-2xl font-bold">O2</div>
        </div>
        <h1 class="text-4xl font-bold leading-tight mb-4">Secure Identity<br/>Made Simple</h1>
        <p class="text-lg text-indigo-200 leading-relaxed max-w-md">
          Enterprise-grade OAuth2 authentication with multi-factor security, single sign-on, and fine-grained access control.
        </p>
        <div class="mt-12 flex items-center gap-6 text-sm text-indigo-300">
          <div class="flex items-center gap-2"><span class="w-2 h-2 bg-emerald-400 rounded-full"></span> RFC 6749 Compliant</div>
          <div class="flex items-center gap-2"><span class="w-2 h-2 bg-emerald-400 rounded-full"></span> OIDC Ready</div>
          <div class="flex items-center gap-2"><span class="w-2 h-2 bg-emerald-400 rounded-full"></span> MFA Support</div>
        </div>
      </div>
    </div>

    <!-- Right Panel: Content -->
    <div class="flex-1 flex flex-col">
      <!-- Mobile Logo -->
      <div class="lg:hidden p-6">
        <AppLogo />
      </div>

      <!-- Form Area -->
      <div class="flex-1 flex items-center justify-center px-6 py-12">
        <div class="w-full max-w-[420px]">
          <slot />
        </div>
      </div>

      <!-- Footer -->
      <div class="px-6 py-4 text-center text-xs text-gray-400">
        <p>© 2026 OAuth2 Platform · <a href="#" class="hover:text-gray-600">Privacy</a> · <a href="#" class="hover:text-gray-600">Terms</a></p>
      </div>
    </div>
  </div>
</template>
