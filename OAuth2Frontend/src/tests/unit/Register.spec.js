
import { mount } from '@vue/test-utils'
import { describe, it, expect, vi } from 'vitest'
import Register from '../../views/Register.vue'

// Mock router
const pushMock = vi.fn()
vi.mock('vue-router', () => ({
    useRouter: () => ({
        push: pushMock
    })
}))

describe('Register.vue', () => {
    it('shows error on password mismatch', async () => {
        const wrapper = mount(Register, {
            global: {
                stubs: { 'router-link': true }
            }
        })

        // Fill required fields first
        await wrapper.find('input[placeholder="Choose a username"]').setValue('newuser')
        await wrapper.find('input[placeholder="you@example.com"]').setValue('test@test.com')

        // Fill passwords with mismatch
        await wrapper.find('input[placeholder="Create a strong password"]').setValue('pass123')
        await wrapper.find('input[placeholder="Confirm your password"]').setValue('pass456')

        await wrapper.find('form').trigger('submit.prevent')

        expect(wrapper.text()).toContain('Passwords do not match')
    })
})
