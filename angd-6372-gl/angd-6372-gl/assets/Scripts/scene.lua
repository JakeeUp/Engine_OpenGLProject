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
