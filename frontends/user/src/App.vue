<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import AuthLayout from './layouts/AuthLayout.vue'

const route = useRoute()
const useAuthLayout = computed(() => route.meta.layout === 'auth')
</script>

<template>
  <AuthLayout v-if="useAuthLayout">
    <router-view />
  </AuthLayout>
  <router-view v-else />
</template>
