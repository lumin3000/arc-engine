
const Illustrations = {

    _sharedQuadMeshId: null,

    _getSharedQuadMesh() {
        if (this._sharedQuadMeshId === null && typeof graphics !== 'undefined') {
            this._sharedQuadMeshId = graphics.create_quad_mesh();
        }
        return this._sharedQuadMeshId;
    },

    drawMesh(mesh, loc, quat, material, layer, scale) {

        if (typeof graphics === 'undefined') {

            this._drawFallback(loc, material);
            return;
        }

        const meshId = this._getSharedQuadMesh();
        if (meshId === null) {
            return;
        }

        if (material?.color) {
            const c = material.color;
            graphics.set_mesh_color(meshId, c.r ?? 1, c.g ?? 1, c.b ?? 1, c.a ?? 1);
        }

        if (material?.shader?.id !== undefined && material.shader.id !== -1 && graphics.set_mesh_shader) {
            graphics.set_mesh_shader(meshId, material.shader.id);
        }

        if (material?.blendMode !== undefined && graphics.set_mesh_blend) {
            graphics.set_mesh_blend(meshId, material.blendMode);
        }

        if (material?.shader?.name && graphics.set_mesh_shader_type) {
            const st = globalThis.ShaderType;
            const shaderTypeNum = st?.[material.shader.name] ?? st?.Textured ?? 9;
            graphics.set_mesh_shader_type(meshId, shaderTypeNum);
        }

        const worldX = (loc.x ?? 0);
        const worldY = (loc.y ?? 0);
        const worldZ = (loc.z ?? 0);

        const flipX = mesh?.flipped ? -1 : 1;
        const sx = (scale?.x ?? 1) * flipX;
        const sy = scale?.y ?? 1;
        const sz = scale?.z ?? 1;

        const texId = material?.textureId ?? 0;

        graphics.draw_mesh(
            meshId,
            worldX, worldY, worldZ,
            quat.x ?? 0, quat.y ?? 0, quat.z ?? 0, quat.w ?? 1,
            sx, sy, sz,
            texId,
            layer ?? 0
        );
    },

    drawMeshAt(mesh, loc, material, layer) {
        if (typeof graphics === 'undefined') {
            this._drawFallback(loc, material);
            return;
        }

        const meshId = this._getSharedQuadMesh();
        if (meshId === null) {
            return;
        }

        if (material?.color) {
            const c = material.color;
            graphics.set_mesh_color(meshId, c.r ?? 1, c.g ?? 1, c.b ?? 1, c.a ?? 1);
        }

        if (material?.shader?.id !== undefined && material.shader.id !== -1 && graphics.set_mesh_shader) {
            graphics.set_mesh_shader(meshId, material.shader.id);
        }

        if (material?.blendMode !== undefined && graphics.set_mesh_blend) {
            graphics.set_mesh_blend(meshId, material.blendMode);
        }

        if (material?.shader?.name && graphics.set_mesh_shader_type) {
            const st = globalThis.ShaderType;
            const shaderTypeNum = st?.[material.shader.name] ?? st?.Textured ?? 9;
            graphics.set_mesh_shader_type(meshId, shaderTypeNum);
        }

        graphics.draw_mesh_at(
            meshId,
            (loc.x ?? 0),
            (loc.y ?? 0),
            (loc.z ?? 0),
            material?.textureId ?? 0,
            layer ?? 0
        );
    },

    _drawFallback(loc, material) {
        if (typeof draw === 'undefined') return;

        const color = material?.color
            ? [material.color.r ?? 1, material.color.g ?? 1, material.color.b ?? 1, material.color.a ?? 1]
            : [1, 1, 1, 1];

        const x = loc.x ?? 0;
        const z = loc.z ?? 0;
        const halfSize = 0.4;

        draw.rect?.(
            x - halfSize, z - halfSize,
            x + halfSize, z + halfSize,
            { col: color, z_layer: draw.ZLAYER_PLAYSPACE ?? 2 }
        );
    },

    freeMesh(mesh) {

    },

    getStats() {
        if (typeof graphics !== 'undefined') {
            return graphics.stats?.() ?? {};
        }
        return {};
    }
};

globalThis.Illustrations = Illustrations;
