# Core-profile (macOS) compatibility for the Cubism GLES2 rendering path.
#
# The macOS build requests an OpenGL core profile context, which differs from
# the compatibility contexts used on Windows and Linux in three ways that the
# GLES2-style Cubism renderer does not expect:
#
#   1. GLSL 1.20 shaders do not compile, so the desktop `Standard` shaders are
#      translated to GLSL 330 at configure time. The translated files replace
#      the SDK ones everywhere the framework shaders are staged.
#   2. Vertex-attributable calls require a bound VAO (handled in the runtime
#      by NativeModel::bind_model_vao).
#   3. Client-side vertex arrays are silently ignored, so DrawMeshOpenGL
#      re-hosts the mesh in stream VBOs.
#
# Everything is guarded by CSM_TARGET_MAC_GL or applied to shader text only,
# so Windows and Linux binaries are unchanged.

set(BONGO_CAT_CUBISM_SHADER_SOURCE_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism-shaders")

function(bongo_cat_core_profile_convert_shader input output)
  file(READ "${input}" text)
  string(REPLACE "\r\n" "\n" text "${text}")
  string(REPLACE "#version 120" "#version 330" text "${text}")
  get_filename_component(name "${input}" NAME)
  if(name STREQUAL "FragShaderSrcColorBlend.frag" OR
      name STREQUAL "FragShaderSrcAlphaBlend.frag")
    # Appended blend-mode snippets: pure functions without legacy syntax.
  elseif(input MATCHES "\\.vert$")
    string(REPLACE "attribute " "in " text "${text}")
    string(REPLACE "varying " "out " text "${text}")
  else()
    string(REPLACE "varying " "in " text "${text}")
    string(REPLACE "texture2D(" "texture(" text "${text}")
    string(REPLACE "gl_FragColor" "BongoCatFragColor" text "${text}")
    string(REPLACE "#version 330\n"
      "#version 330\n\nout vec4 BongoCatFragColor;\n" text "${text}")
  endif()
  foreach(legacy IN ITEMS "attribute " "varying " "texture2D(" "gl_FragColor"
      "#version 120")
    string(FIND "${text}" "${legacy}" position)
    if(NOT position EQUAL -1)
      message(FATAL_ERROR "Core-profile shader conversion failed for ${name}: "
        "'${legacy}' remains")
    endif()
  endforeach()
  file(WRITE "${output}" "${text}")
endfunction()

function(bongo_cat_core_profile_prepare_shaders)
  set(source_dir "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard")
  file(MAKE_DIRECTORY "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}")
  file(GLOB shader_files "${source_dir}/*")
  foreach(input IN LISTS shader_files)
    get_filename_component(name "${input}" NAME)
    bongo_cat_core_profile_convert_shader("${input}"
      "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}/${name}")
  endforeach()
endfunction()

function(bongo_cat_core_profile_patch_renderer target)
  set(source_path "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/CubismRenderer_OpenGLES2.cpp")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism")
  set(output_source "${output_dir}/CubismRenderer_OpenGLES2.cpp")
  file(READ "${source_path}" source)
  string(REPLACE "\r\n" "\n" source "${source}")

  set(legacy_draw [=[
        csmInt32 indexCount = model.GetDrawableVertexIndexCount(index);
        csmUint16* indexArray = const_cast<csmUint16*>(model.GetDrawableVertexIndices(index));
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indexArray);]=])
  set(vbo_draw [=[
        csmInt32 indexCount = model.GetDrawableVertexIndexCount(index);
        csmUint16* indexArray = const_cast<csmUint16*>(model.GetDrawableVertexIndices(index));
#if defined(CSM_TARGET_MAC_GL)
        // Core-profile contexts reject client-side vertex arrays, so the
        // GLES2-style mesh pointers are re-hosted in reused stream VBOs.
        static GLuint meshVbo[3] = {0, 0, 0};
        if (meshVbo[0] == 0)
        {
            glGenBuffers(3, meshVbo);
        }
        GLint positionLocation = glGetAttribLocation(currentProgram, "a_position");
        GLint texCoordLocation = glGetAttribLocation(currentProgram, "a_texCoord");
        csmInt32 vertexCount = model.GetDrawableVertexCount(index);
        if (positionLocation >= 0)
        {
            glBindBuffer(GL_ARRAY_BUFFER, meshVbo[0]);
            glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(csmFloat32) * 2,
                model.GetDrawableVertices(index), GL_STREAM_DRAW);
            glVertexAttribPointer(positionLocation, 2, GL_FLOAT, GL_FALSE,
                sizeof(csmFloat32) * 2, NULL);
        }
        if (texCoordLocation >= 0)
        {
            glBindBuffer(GL_ARRAY_BUFFER, meshVbo[1]);
            glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(csmFloat32) * 2,
                model.GetDrawableVertexUvs(index), GL_STREAM_DRAW);
            glVertexAttribPointer(texCoordLocation, 2, GL_FLOAT, GL_FALSE,
                sizeof(csmFloat32) * 2, NULL);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshVbo[2]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(csmUint16),
            indexArray, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, NULL);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#else
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indexArray);
#endif]=])
  string(FIND "${source}" "${legacy_draw}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Core-profile renderer patch mismatch: mesh draw")
  endif()
  string(REPLACE "${legacy_draw}" "${vbo_draw}" source "${source}")

  file(MAKE_DIRECTORY "${output_dir}")
  file(WRITE "${output_source}" "${source}")
  get_target_property(framework_sources ${target} SOURCES)
  list(REMOVE_ITEM framework_sources "${source_path}")
  set_property(TARGET ${target} PROPERTY SOURCES "${framework_sources}")
  target_sources(${target} PRIVATE "${output_source}")
  target_include_directories(${target} PRIVATE
    "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL")
endfunction()
