#include "ForwardShadingCommon.hlsl"
#include "Tonemap.hlsl"

Texture2D DiffuseMap : register(t0);
SamplerState DiffuseSamplerState : register(s0);

TextureCube EnvironmentMap : register(t1);

float4 PSMain(VertexOut PIn) : SV_Target
{
    //Base color

    //Shadow
    
    //Diffuse BRDF
    
    //Specular BRDF
    
    //Emissive
    
    //Sky light
    
    //Fog

    float3 Color = 0;

    // BaseColor Specular Emissive is sRGB converted to linear by D3DSAMP_SRGBTEXTURE of dx an GL_TEXTURE_SRGB_DECODE_EXT of gl automatically 
    float Opacity = 1.0f;
    float3 BaseColor = gBaseColor.rgb;
    float Metallic = gMetallic;
    float Specular = gSpecular;
    float Roughness = gRoughness;
    float3 Emissive = gEmissiveColor.rgb;

    float4 diffuse = DiffuseMap.Sample(DiffuseSamplerState, PIn.TexCoord);

    BaseColor = diffuse.rgb;
    
    float DielectricSpecular = 0.08 * Specular;
    float3 DiffuseColor = BaseColor - BaseColor * Metallic; // 1 mad
    float3 SpecularColor = (DielectricSpecular - DielectricSpecular * Metallic) + BaseColor * Metallic; // 2 mad

    float3 worldNormal = PIn.Normal;
    float3 directionalLightDirection = gDirectionalLightDir;
    float3 reflectionVector = worldNormal;
    float3 cameraVector = -gCameraDir;
    float3 DirectionalLightColor = gDirectionalLightColor.rgb;
 
    float NoV = max(0, dot(worldNormal, cameraVector));
    float NoL = max(0, dot(worldNormal, directionalLightDirection));
    float RoL = max(0, dot(reflectionVector, directionalLightDirection));
    float3 H = normalize(cameraVector + directionalLightDirection);
    float NoH = max(0, dot(worldNormal, H));
    float VoH = max(0, dot(cameraVector, H));
    float VoL = max(0, dot(cameraVector, directionalLightDirection));

    BxDFContext context;
    context.NoV = NoV;
    context.NoL = NoL;
    context.VoL = VoL;
    context.NoH = NoH;
    context.VoH = VoH;

    float3 DirectSpecularPart = SpecularGGX(Roughness, SpecularColor, context, NoL) * NoL;
    Color += NoL * DirectionalLightColor.rgb * DiffuseColor + DirectSpecularPart;
    float3 r = reflect(-gCameraPos, reflectionVector);
 	// Compute fractional mip from roughness
    float AbsoluteSpecularMip = ComputeReflectionCaptureMipFromRoughness(Roughness, 10.0f);
    float4 reflectionColor = EnvironmentMap.SampleLevel(DiffuseSamplerState, r, AbsoluteSpecularMip);
    float3 IndirectSpecularPart = EnvBRDFApprox(SpecularColor, Roughness, NoV);
    Color += IndirectSpecularPart * RGBMDecode(reflectionColor, 16.0f);
    Color += Emissive;
    
    return float4(ACESToneMapping(Color.rgb, 1.0f), 1.0f);
}

