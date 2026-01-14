import { createRouter, createWebHistory } from 'vue-router'
import Login from '../views/Login.vue'
import Callback from '../views/Callback.vue'

const router = createRouter({
    history: createWebHistory(import.meta.env.BASE_URL),
    routes: [
        {
            path: '/',
            name: 'home',
            component: Login
        },
        {
            path: '/callback',
            name: 'callback',
            component: Callback
        }
    ]
})

export default router
