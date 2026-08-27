<script setup lang="ts">
import AppLogo from '../components/shared/AppLogo.vue'
</script>

<template>
  <div class="min-h-screen flex">
    <!-- Left Panel: Brand -->
    <div class="hidden lg:flex lg:w-1/2 bg-gradient-to-br from-sky-700 via-sky-800 to-indigo-900 relative overflow-hidden">
      <!-- Decorative background pattern -->
      <div class="absolute inset-0 opacity-[0.04]">
        <svg
          class="w-full h-full"
          viewBox="0 0 600 600"
          fill="none"
          xmlns="http://www.w3.org/2000/svg"
        >
          <circle
            cx="300"
            cy="300"
            r="250"
            stroke="white"
            stroke-width="0.5"
          />
          <circle
            cx="300"
            cy="300"
            r="180"
            stroke="white"
            stroke-width="0.5"
          />
          <circle
            cx="300"
            cy="300"
            r="110"
            stroke="white"
            stroke-width="0.5"
          />
          <line
            x1="50"
            y1="300"
            x2="550"
            y2="300"
            stroke="white"
            stroke-width="0.3"
          />
          <line
            x1="300"
            y1="50"
            x2="300"
            y2="550"
            stroke="white"
            stroke-width="0.3"
          />
        </svg>
      </div>

      <div class="relative z-10 flex flex-col justify-center px-16 text-white">
        <div class="mb-10">
          <div class="w-12 h-12 rounded-xl bg-white/20 backdrop-blur-sm flex items-center justify-center text-2xl font-bold tracking-tight">
            AF
          </div>
        </div>
        <h1 class="text-[2.5rem] font-bold leading-tight tracking-tight mb-5">
          Secure Identity<br>Made Simple
        </h1>
        <p class="text-lg text-sky-200 leading-relaxed max-w-md">
          Enterprise-grade OAuth2 authentication with multi-factor security, single sign-on, and fine-grained access control.
        </p>

        <!-- Feature indicators -->
        <div class="mt-12 flex flex-wrap items-center gap-6 text-sm text-sky-300">
          <div class="flex items-center gap-2">
            <svg
              class="w-4 h-4 text-emerald-400"
              viewBox="0 0 16 16"
              fill="currentColor"
            >
              <path
                fill-rule="evenodd"
                d="M12.416 3.376a.75.75 0 01.208 1.04l-5 7.5a.75.75 0 01-1.154.114l-3-3a.75.75 0 011.06-1.06l2.353 2.353 4.493-6.74a.75.75 0 011.04-.207z"
              />
            </svg>
            RFC 6749 Compliant
          </div>
          <div class="flex items-center gap-2">
            <svg
              class="w-4 h-4 text-emerald-400"
              viewBox="0 0 16 16"
              fill="currentColor"
            >
              <path
                fill-rule="evenodd"
                d="M12.416 3.376a.75.75 0 01.208 1.04l-5 7.5a.75.75 0 01-1.154.114l-3-3a.75.75 0 011.06-1.06l2.353 2.353 4.493-6.74a.75.75 0 011.04-.207z"
              />
            </svg>
            OIDC Ready
          </div>
          <div class="flex items-center gap-2">
            <svg
              class="w-4 h-4 text-emerald-400"
              viewBox="0 0 16 16"
              fill="currentColor"
            >
              <path
                fill-rule="evenodd"
                d="M12.416 3.376a.75.75 0 01.208 1.04l-5 7.5a.75.75 0 01-1.154.114l-3-3a.75.75 0 011.06-1.06l2.353 2.353 4.493-6.74a.75.75 0 011.04-.207z"
              />
            </svg>
            MFA Support
          </div>
        </div>
      </div>
    </div>

    <!-- Right Panel: Content -->
    <div class="flex-1 flex flex-col bg-white">
      <!-- Mobile Logo -->
      <div class="lg:hidden p-6 border-b border-neutral-100">
        <AppLogo />
      </div>

      <!-- Form Area -->
      <div class="flex-1 flex items-center justify-center px-6 py-12">
        <div class="w-full max-w-[420px]">
          <slot />
        </div>
      </div>

      <!-- Footer -->
      <div class="px-6 py-4 text-center text-xs text-neutral-400 border-t border-neutral-50">
        <p>
          Fulla Identity Platform &middot;
          <a
            href="#"
            class="hover:text-neutral-600 transition-colors"
          >Privacy</a> &middot;
          <a
            href="#"
            class="hover:text-neutral-600 transition-colors"
          >Terms</a>
        </p>
      </div>
    </div>
  </div>
</template>
