<script setup lang="ts">
import { useAuthStore } from '../../stores/auth'

const auth = useAuthStore()
</script>

<template>
  <div>
    <h1 class="text-2xl font-bold text-gray-900 mb-6">Dashboard</h1>

    <div class="bg-white rounded-xl shadow-sm border border-gray-200 p-6 mb-6">
      <h2 class="text-lg font-semibold text-gray-900">Welcome, {{ auth.user?.name || 'User' }}</h2>
      <p class="text-gray-500 mt-1">Here is an overview of your account.</p>
    </div>

    <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
      <div class="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
        <p class="text-xs font-medium text-gray-500 uppercase">Account ID</p>
        <p class="mt-2 text-sm font-mono text-gray-800 break-all">{{ auth.user?.sub || 'N/A' }}</p>
      </div>
      <div class="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
        <p class="text-xs font-medium text-gray-500 uppercase">Email</p>
        <p class="mt-2 text-sm text-gray-800">{{ auth.user?.email || 'N/A' }}</p>
      </div>
      <div class="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
        <p class="text-xs font-medium text-gray-500 uppercase">Roles</p>
        <div class="mt-2 flex flex-wrap gap-1">
          <span v-for="role in (auth.user?.roles || [])" :key="role"
            class="px-2 py-0.5 text-xs font-medium rounded-full bg-indigo-100 text-indigo-700">
            {{ role }}
          </span>
          <span v-if="!auth.user?.roles?.length" class="text-sm text-gray-400">None</span>
        </div>
      </div>
    </div>

    <div class="mt-8 grid grid-cols-1 md:grid-cols-3 gap-4">
      <router-link to="/profile" class="block p-5 bg-white rounded-xl border border-gray-200 hover:border-indigo-300 hover:shadow-md transition-all">
        <p class="font-medium text-gray-900">Edit Profile</p>
        <p class="text-sm text-gray-500 mt-1">Update your personal information</p>
      </router-link>
      <router-link to="/security" class="block p-5 bg-white rounded-xl border border-gray-200 hover:border-indigo-300 hover:shadow-md transition-all">
        <p class="font-medium text-gray-900">Security Settings</p>
        <p class="text-sm text-gray-500 mt-1">Manage MFA and password</p>
      </router-link>
      <router-link to="/authorized-apps" class="block p-5 bg-white rounded-xl border border-gray-200 hover:border-indigo-300 hover:shadow-md transition-all">
        <p class="font-medium text-gray-900">Authorized Apps</p>
        <p class="text-sm text-gray-500 mt-1">Review connected applications</p>
      </router-link>
    </div>
  </div>
</template>
