import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '../stores/auth'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    // Auth pages (AuthLayout)
    {
      path: '/login',
      name: 'login',
      component: () => import('../pages/auth/LoginPage.vue'),
      meta: { guest: true, layout: 'auth' },
    },
    {
      path: '/register',
      name: 'register',
      component: () => import('../pages/auth/RegisterPage.vue'),
      meta: { guest: true, layout: 'auth' },
    },
    {
      path: '/forgot-password',
      name: 'forgot-password',
      component: () => import('../pages/auth/ForgotPasswordPage.vue'),
      meta: { guest: true, layout: 'auth' },
    },
    {
      path: '/reset-password',
      name: 'reset-password',
      component: () => import('../pages/auth/ResetPasswordPage.vue'),
      meta: { guest: true, layout: 'auth' },
    },
    {
      path: '/verify-email',
      name: 'verify-email',
      component: () => import('../pages/auth/VerifyEmailPage.vue'),
      meta: { layout: 'auth' },
    },

    // OAuth protocol pages (standalone)
    {
      path: '/callback',
      name: 'callback',
      component: () => import('../pages/oauth/CallbackPage.vue'),
    },
    {
      path: '/callback/github',
      name: 'github-callback',
      component: () => import('../pages/oauth/GitHubCallbackPage.vue'),
    },
    {
      path: '/consent',
      name: 'consent',
      component: () => import('../pages/oauth/ConsentPage.vue'),
      meta: { layout: 'auth' },
    },
    {
      path: '/device/verify',
      name: 'device-verify',
      component: () => import('../pages/oauth/DeviceVerifyPage.vue'),
      meta: { layout: 'auth' },
    },

    // Protected account pages (AppLayout)
    {
      path: '/',
      component: () => import('../layouts/AppLayout.vue'),
      meta: { auth: true },
      children: [
        { path: '', name: 'dashboard', component: () => import('../pages/account/DashboardPage.vue') },
        { path: 'profile', name: 'profile', component: () => import('../pages/account/ProfilePage.vue') },
        { path: 'security', name: 'security', component: () => import('../pages/account/SecurityPage.vue') },
        { path: 'authorized-apps', name: 'authorized-apps', component: () => import('../pages/account/AuthorizedAppsPage.vue') },
      ],
    },
  ],
})

router.beforeEach(async (to, _from, next) => {
  const auth = useAuthStore()
  
  // Try to restore session on first navigation to protected route
  if (to.meta.auth && !auth.isAuthenticated) {
    const restored = await auth.restoreSession()
    if (restored) {
      next()
      return
    }
    next({ name: 'login', query: { redirect: to.fullPath } })
  } else if (to.meta.guest && auth.isAuthenticated) {
    next({ name: 'dashboard' })
  } else {
    next()
  }
})

export default router
