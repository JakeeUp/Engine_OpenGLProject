scene = {
    mesh_instances = {
        {
            name = 'Human',
            position = { 0.0, 0.0, 0.0 },
            rotation = { 0.0, 0.0, 0.0 },
            scale    = { 0.1, 0.1, 0.1 },
            mesh = 'human'
        },
        {
            name = 'Koenigsegg',
            position = { 3.0, 0.0, -1.0 },
            rotation = { -90.0, 0.0, 0.0 },
            scale    = { 0.12, 0.12, 0.12 },
            mesh = 'car'
        },
        {
            name = 'LivingRoom',
            position = { 0.0, 0.0, 0.0 },
            rotation = { 0.0, 0.0, 0.0 },
            scale    = { 1.0, 1.0, 1.0 },
            mesh = 'room'
        }
    },

    global_ambient = { 0.08, 0.08, 0.1, 1.0 },

    lights = {
        -- Key light: warm, strong, upper-right front
        {
            enabled = true,
            position = { 3.0, 4.0, 3.0, 1.0 },
            ambient = { 0.05, 0.04, 0.03, 1.0 },
            diffuse = { 1.0, 0.92, 0.82, 1.0 },
            specular = { 1.0, 1.0, 1.0, 1.0 },
            intensity = 2.0,
            atten_const = 1.0,
            atten_linear = 0.07,
            atten_quad = 0.01
        },
        -- Fill light: cool blue, softer, left side
        {
            enabled = true,
            position = { -3.0, 2.5, 1.0, 1.0 },
            ambient = { 0.02, 0.02, 0.04, 1.0 },
            diffuse = { 0.4, 0.5, 0.8, 1.0 },
            specular = { 0.5, 0.5, 0.7, 1.0 },
            intensity = 1.2,
            atten_const = 1.0,
            atten_linear = 0.09,
            atten_quad = 0.01
        },
        -- Rim/back light: bright white, behind and above
        {
            enabled = true,
            position = { -0.5, 5.0, -3.0, 1.0 },
            ambient = { 0.0, 0.0, 0.0, 1.0 },
            diffuse = { 0.9, 0.95, 1.0, 1.0 },
            specular = { 1.0, 1.0, 1.0, 1.0 },
            intensity = 1.8,
            atten_const = 1.0,
            atten_linear = 0.05,
            atten_quad = 0.01
        },
        -- Ground bounce: subtle warm from below
        {
            enabled = true,
            position = { 0.0, 0.3, 2.0, 1.0 },
            ambient = { 0.0, 0.0, 0.0, 1.0 },
            diffuse = { 0.6, 0.5, 0.35, 1.0 },
            specular = { 0.3, 0.3, 0.2, 1.0 },
            intensity = 0.5,
            atten_const = 1.0,
            atten_linear = 0.2,
            atten_quad = 0.05
        },
        -- Accent: magenta/purple from the side for drama
        {
            enabled = true,
            position = { -4.0, 3.0, -1.0, 1.0 },
            ambient = { 0.0, 0.0, 0.0, 1.0 },
            diffuse = { 0.7, 0.2, 0.6, 1.0 },
            specular = { 1.0, 0.5, 0.8, 1.0 },
            intensity = 0.8,
            atten_const = 1.0,
            atten_linear = 0.14,
            atten_quad = 0.02
        },
        {
            enabled = false,
            position = { 3.0, 2.0, 3.0, 1.0 },
            ambient = { 0.0, 0.0, 0.05, 1.0 },
            diffuse = { 0.4, 0.4, 1.0, 1.0 },
            specular = { 1.0, 1.0, 1.0, 1.0 },
            intensity = 1.0,
            atten_const = 1.0,
            atten_linear = 0.1,
            atten_quad = 0.03
        },
        {
            enabled = false,
            position = { 2.0, 5.0, -2.0, 1.0 },
            ambient = { 0.05, 0.05, 0.0, 1.0 },
            diffuse = { 1.0, 1.0, 0.6, 1.0 },
            specular = { 1.0, 1.0, 1.0, 1.0 },
            intensity = 0.7,
            atten_const = 1.0,
            atten_linear = 0.25,
            atten_quad = 0.03
        },
        {
            enabled = false,
            position = { -3.0, 5.0, 3.0, 1.0 },
            ambient = { 0.05, 0.0, 0.05, 1.0 },
            diffuse = { 1.0, 0.4, 1.0, 1.0 },
            specular = { 1.0, 1.0, 1.0, 1.0 },
            intensity = 1.0,
            atten_const = 1.0,
            atten_linear = 0.15,
            atten_quad = 0.04
        }
    }
}

-- ─── Stress test ────────────────────────────────────────────────────────────
-- Spawns a grid of extra human meshes to measure how draw submission and GPU
-- cost scale with instance count. Set STRESS_COUNT to 0 to disable.
--
-- NOTE: this Lua state only opens the base and math libraries (see
-- open_libraries in main.cpp), so no table.insert and no string.format here.
local STRESS_COUNT = 0
local PER_ROW      = 10
local SPACING_X    = 1.4
local SPACING_Z    = 1.6

local n = #scene.mesh_instances
for i = 0, STRESS_COUNT - 1 do
    local col = i % PER_ROW
    local row = math.floor(i / PER_ROW)
    n = n + 1
    scene.mesh_instances[n] = {
        name     = 'StressHuman_' .. i,
        position = { -7.0 + col * SPACING_X, 0.0, -4.0 - row * SPACING_Z },
        rotation = { 0.0, 0.0, 0.0 },
        scale    = { 0.1, 0.1, 0.1 },
        mesh     = 'human'
    }
end
