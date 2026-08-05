<script setup lang="ts">
import { useAuthStore } from '../../stores/auth'

const auth = useAuthStore()
</script>

<template>
  <div class="space-y-8">
    <!-- Header -->
    <div>
      <h1 class="text-2xl font-bold text-neutral-900 tracking-tight">Dashboard</h1>
      <p class="mt-1 text-sm text-neutral-500">Welcome back{{ auth.user?.name ? ', ' + auth.user.name : '' }}</p>
    </div>

    <!-- Welcome Card -->
    <div class="bg-white rounded-xl border border-neutral-200 shadow-sm p-6">
      <div class="flex items-center gap-4">
        <div class="w-12 h-12 rounded-full bg-sky-100 text-sky-700 flex items-center justify-center text-sm font-semibold">
          {{ (auth.user?.name || 'U')[0].toUpperCase() }}
        </div>
        <div>
          <h2 class="text-lg font-semibold text-neutral-900">{{ auth.user?.name || 'User' }}</h2>
          <p class="text-sm text-neutral-500">{{ auth.user?.email || 'No email' }}</p>
        </div>
      </div>
    </div>

    <!-- Account Overview Cards -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-5">
      <div class="bg-white rounded-xl border border-neutral-200 p-5">
        <p class="text-xs font-semibold text-neutral-500 uppercase tracking-wider mb-3">Account ID</p>
        <p class="text-sm font-mono text-neutral-800 break-all">{{ auth.user?.sub || 'N/A' }}</p>
      </div>

      <div class="bg-white rounded-xl border border-neutral-200 p-5">
        <p class="text-xs font-semibold text-neutral-500 uppercase tracking-wider mb-3">Email</p>
        <p class="text-sm text-neutral-800">{{ auth.user?.email || 'N/A' }}</p>
      </div>

      <div class="bg-white rounded-xl border border-neutral-200 p-5">
        <p class="text-xs font-semibold text-neutral-500 uppercase tracking-wider mb-3">Roles</p>
        <div class="flex flex-wrap gap-1.5">
          <span
            v-for="role in (auth.user?.roles || [])"
            :key="role"
            class="px-2.5 py-0.5 text-xs font-medium rounded-full bg-sky-50 text-sky-700 border border-sky-100"
          >
            {{ role }}
          </span>
          <span v-if="!auth.user?.roles?.length" class="text-sm text-neutral-400">None</span>
        </div>
      </div>
    </div>

    <!-- Quick Links -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
      <router-link
        to="/profile"
        class="group p-5 bg-white rounded-xl border border-neutral-200 hover:border-sky-300 hover:shadow-sm transition-all duration-150"
      >
        <div class="flex items-center gap-3 mb-2">
          <div class="w-9 h-9 rounded-lg bg-sky-50 flex items-center justify-center group-hover:bg-sky-100 transition-colors">
            <svg class="w-5 h-5 text-sky-600" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M7 8a3 3 0 100-6 3 3 0 000 6zm-2.5 3.5A3.5 3.5 0 001 15v.75a.75.75 0 001.5 0V15a2 2 0 012-2h7a2 2 0 012 2v.75a.75.75 0 001.5 0V15a3.5 3.5 0 00-3.5-3.5h-7z"/>
            </svg>
          </div>
          <p class="font-medium text-neutral-900">Edit Profile</p>
        </div>
        <p class="text-sm text-neutral-500">Update your personal information</p>
      </router-link>

      <router-link
        to="/security"
        class="group p-5 bg-white rounded-xl border border-neutral-200 hover:border-sky-300 hover:shadow-sm transition-all duration-150"
      >
        <div class="flex items-center gap-3 mb-2">
          <div class="w-9 h-9 rounded-lg bg-amber-50 flex items-center justify-center group-hover:bg-amber-100 transition-colors">
            <svg class="w-5 h-5 text-amber-600" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M8 2a3.5 3.5 0 00-3.5 3.5v2.382l-.964.643A1.5 1.5 0 003 9.862v.638a1.5 1.5 0 001.5 1.5h7a1.5 1.5 0 001.5-1.5v-.638a1.5 1.5 0 00-.536-1.137l-.964-.643V5.5A3.5 3.5 0 008 2z"/>
            </svg>
          </div>
          <p class="font-medium text-neutral-900">Security Settings</p>
        </div>
        <p class="text-sm text-neutral-500">Manage MFA, password, and passkeys</p>
      </router-link>

      <router-link
        to="/authorized-apps"
        class="group p-5 bg-white rounded-xl border border-neutral-200 hover:border-sky-300 hover:shadow-sm transition-all duration-150"
      >
        <div class="flex items-center gap-3 mb-2">
          <div class="w-9 h-9 rounded-lg bg-emerald-50 flex items-center justify-center group-hover:bg-emerald-100 transition-colors">
            <svg class="w-5 h-5 text-emerald-600" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M2.75 4.5A2.25 2.25 0 015 2.25h2.5A2.25 2.25 0 019.75 4.5v2.5A2.25 2.25 0 017.5 9.25H5a2.25 2.25 0 01-2.25-2.25v-2.5z"/>
            </svg>
          </div>
          <p class="font-medium text-neutral-900">Authorized Apps</p>
        </div>
        <p class="text-sm text-neutral-500">Review connected applications</p>
      </router-link>
    </div>
  </div>
</template>
