<script setup lang="ts">
import { useAuthStore } from '../../stores/auth'
import AppBadge from '../../components/ui/AppBadge.vue'
import AppCard from '../../components/ui/AppCard.vue'
import DData from '../../components/ui/DData.vue'

const auth = useAuthStore()
</script>

<template>
  <div class="space-y-8">
    <!-- Header -->
    <div>
      <h1 class="text-2xl font-bold text-neutral-900 tracking-tight">
        {{ $t('account.dashboard.title') }}
      </h1>
      <p class="mt-1 text-sm text-neutral-500">
        {{ auth.user?.name
          ? $t('account.dashboard.welcomeBackName', { name: auth.user.name })
          : $t('account.dashboard.welcomeBack') }}
      </p>
    </div>

    <!-- Welcome Card -->
    <AppCard>
      <div class="flex items-center gap-4">
        <div class="w-12 h-12 rounded-full bg-brand-100 text-brand-700 flex items-center justify-center text-sm font-semibold">
          {{ (auth.user?.name || 'U')[0].toUpperCase() }}
        </div>
        <div>
          <h2 class="text-lg font-semibold text-neutral-900">
            {{ auth.user?.name || $t('common.user') }}
          </h2>
          <p class="text-sm text-neutral-500">
            {{ auth.user?.email || $t('account.dashboard.noEmail') }}
          </p>
        </div>
      </div>
    </AppCard>

    <!-- Account Overview Cards -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-5">
      <AppCard padding="sm">
        <p class="text-xs font-semibold text-neutral-500 uppercase tracking-wider mb-3">
          {{ $t('account.dashboard.accountId') }}
        </p>
        <DData
          :value="auth.user?.sub || 'N/A'"
          truncate
        />
      </AppCard>

      <AppCard padding="sm">
        <p class="text-xs font-semibold text-neutral-500 uppercase tracking-wider mb-3">
          {{ $t('common.email') }}
        </p>
        <p class="text-sm text-neutral-800">
          {{ auth.user?.email || 'N/A' }}
        </p>
      </AppCard>

      <AppCard padding="sm">
        <p class="text-xs font-semibold text-neutral-500 uppercase tracking-wider mb-3">
          {{ $t('common.roles') }}
        </p>
        <div class="flex flex-wrap gap-1.5">
          <AppBadge
            v-for="role in (auth.user?.roles || [])"
            :key="role"
            variant="info"
            size="sm"
          >
            {{ role }}
          </AppBadge>
          <span
            v-if="!auth.user?.roles?.length"
            class="text-sm text-neutral-400"
          >{{ $t('account.dashboard.noRoles') }}</span>
        </div>
      </AppCard>
    </div>

    <!-- Quick Links -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
      <router-link
        to="/profile"
        class="group p-5 bg-surface rounded-card border border-neutral-200 hover:border-brand-300 hover:shadow-sm transition-all duration-150
               focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
      >
        <div class="flex items-center gap-3 mb-2">
          <div class="w-9 h-9 rounded-lg bg-brand-50 flex items-center justify-center group-hover:bg-brand-100 transition-colors">
            <svg
              class="w-5 h-5 text-brand-700"
              viewBox="0 0 20 20"
              fill="currentColor"
            >
              <path
                fill-rule="evenodd"
                d="M7 8a3 3 0 100-6 3 3 0 000 6zm-2.5 3.5A3.5 3.5 0 001 15v.75a.75.75 0 001.5 0V15a2 2 0 012-2h7a2 2 0 012 2v.75a.75.75 0 001.5 0V15a3.5 3.5 0 00-3.5-3.5h-7z"
              />
            </svg>
          </div>
          <p class="font-medium text-neutral-900">
            {{ $t('account.dashboard.editProfile') }}
          </p>
        </div>
        <p class="text-sm text-neutral-500">
          {{ $t('account.dashboard.editProfileDesc') }}
        </p>
      </router-link>

      <router-link
        to="/security"
        class="group p-5 bg-surface rounded-card border border-neutral-200 hover:border-brand-300 hover:shadow-sm transition-all duration-150
               focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
      >
        <div class="flex items-center gap-3 mb-2">
          <div class="w-9 h-9 rounded-lg bg-warning-50 flex items-center justify-center group-hover:bg-warning-100 transition-colors">
            <svg
              class="w-5 h-5 text-warning-600"
              viewBox="0 0 20 20"
              fill="currentColor"
            >
              <path
                fill-rule="evenodd"
                d="M8 2a3.5 3.5 0 00-3.5 3.5v2.382l-.964.643A1.5 1.5 0 003 9.862v.638a1.5 1.5 0 001.5 1.5h7a1.5 1.5 0 001.5-1.5v-.638a1.5 1.5 0 00-.536-1.137l-.964-.643V5.5A3.5 3.5 0 008 2z"
              />
            </svg>
          </div>
          <p class="font-medium text-neutral-900">
            {{ $t('account.dashboard.securityTitle') }}
          </p>
        </div>
        <p class="text-sm text-neutral-500">
          {{ $t('account.dashboard.securityDesc') }}
        </p>
      </router-link>

      <router-link
        to="/authorized-apps"
        class="group p-5 bg-surface rounded-card border border-neutral-200 hover:border-brand-300 hover:shadow-sm transition-all duration-150
               focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
      >
        <div class="flex items-center gap-3 mb-2">
          <div class="w-9 h-9 rounded-lg bg-success-50 flex items-center justify-center group-hover:bg-success-100 transition-colors">
            <svg
              class="w-5 h-5 text-success-600"
              viewBox="0 0 20 20"
              fill="currentColor"
            >
              <path
                fill-rule="evenodd"
                d="M2.75 4.5A2.25 2.25 0 015 2.25h2.5A2.25 2.25 0 019.75 4.5v2.5A2.25 2.25 0 017.5 9.25H5a2.25 2.25 0 01-2.25-2.25v-2.5z"
              />
            </svg>
          </div>
          <p class="font-medium text-neutral-900">
            {{ $t('nav.authorizedApps') }}
          </p>
        </div>
        <p class="text-sm text-neutral-500">
          {{ $t('account.dashboard.authorizedAppsDesc') }}
        </p>
      </router-link>
    </div>
  </div>
</template>
