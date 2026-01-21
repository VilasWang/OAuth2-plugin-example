
import { mount, flushPromises } from '@vue/test-utils'
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import Callback from '../../views/Callback.vue'

// Mock key vue-router hooks before importing component
vi.mock('vue-router', () => ({
    useRoute: vi.fn(() => ({
        query: { code: 'test_code', state: 'test_state' }
    })),
    useRouter: vi.fn(() => ({
        push: vi.fn()
    }))
}))

describe('Callback.vue', () => {
    beforeEach(() => {
        // Mock fetch
        global.fetch = vi.fn(() =>
            Promise.resolve({
                ok: true,
                json: () => Promise.resolve({ access_token: 'test_token', user: { name: 'tester' } }),
            })
        )

        // Mock localStorage
        vi.spyOn(Storage.prototype, 'getItem').mockImplementation((key) => {
            if (key === 'auth_state_drogon') return 'test_state';
            return null; // Return null for auth_provider -> defaults to drogon
        })
        vi.spyOn(Storage.prototype, 'removeItem').mockImplementation(() => { })
    })

    afterEach(() => {
        vi.restoreAllMocks()
    })

    it('exchanges code for token on mount', async () => {
        const wrapper = mount(Callback, {
            global: {
                stubs: { 'router-link': true }
            }
        })

        await flushPromises() // Wait for promises to resolve

        // Check if fetch called with correct params
        expect(global.fetch).toHaveBeenCalled()
        // More specific check
        const calls = global.fetch.mock.calls;
        const tokenCall = calls.find(call => call[0].includes('/oauth2/token'));
        expect(tokenCall).toBeDefined();
        expect(tokenCall[1].body.toString()).toContain('code=test_code');
    })
})
