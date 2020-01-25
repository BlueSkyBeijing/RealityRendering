//Diffuse Light
//Lambert

float3 Lambert(float3 normal, float3 lightDir, float3 lightColor)
{
	float NdotL = max(0.0, dot(normal, lightDir));
	return lightColor * NdotL;
}

//Half-Lambert

float3 HalfLambert(float3 normal, float3 lightDir, float3 lightColor)
{
	float NdotL = max(0.0, dot(normal, lightDir));
	float HalfLambertDiffuse = pow(NdotL * 0.5 + 0.5, 2.0);
	return lightColor * HalfLambertDiffuse;
}


// Specular
// Phong

float3 Phong(float3 normal, float3 lightDir, float3 lightColor, float3 specularColor, float specPower)
{
	float NdotL = max(0, dot(normal, lightDir));
	 
	float3 lightReflectDirection = reflect(-lightDir, normal);
	float RdotV = max(0, dot(lightReflectDirection, viewDir));
	float spec = pow(RdotV, specPower/4) * specularColor;	 
	return lightColor.rgb * NdotL) + (lightColor.rgb * specularColor.rgb * spec);
}

// Blinn-Phong

float3 BlinnPhong(float3 normal, float3 lightDir, float3 lightColor, float3 specularColor, float specPower)
{
	float NdotL = max(0, dot(normal, lightDir));
	 
	float3 halfVector = normalize(lightDir + viewDir);
	float NdotH = max(0, dot(normal, halfVector));
	float spec = pow(NdotH, specPower) * specularColor;
	 
	return lightColor.rgb * NdotL) + (lightColor.rgb * specularColor.rgb * spec);
}

// BRDF
// Empirical Models
// Data-driven Models
// Physical-based Models

// Helmholtz Recoprpcity Rule

// Physically based shading model
// parameterized with the below options

// Microfacet specular = D*G*F / (4*NoL*NoV) = D*Vis*F
// Vis = G / (4*NoL*NoV)

float3 Diffuse_Lambert(float3 DiffuseColor)
{
	return DiffuseColor * (1 / PI);
}

// [Burley 2012, "Physically-Based Shading at Disney"]
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * Pow5(1 - NoV);
	float FdL = 1 + (FD90 - 1) * Pow5(1 - NoL);
	return DiffuseColor * ((1 / PI) * FdV * FdL);
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
float3 Diffuse_OrenNayar(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float a = Roughness * Roughness;
	float s = a;// / ( 1.29 + 0.5 * a );
	float s2 = s * s;
	float VoL = 2 * VoH * VoH - 1;		// double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * (Cosri >= 0 ? rcp(max(NoL, NoV)) : 1);
	return DiffuseColor / PI * (C1 + C2) * (1 + Roughness * 0.5);
}

// [Gotanda 2014, "Designing Reflectance Models for New Consoles"]
float3 Diffuse_Gotanda(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float a = Roughness * Roughness;
	float a2 = a * a;
	float F0 = 0.04;
	float VoL = 2 * VoH * VoH - 1;		// double angle identity
	float Cosri = VoL - NoV * NoL;
#if 1
	float a2_13 = a2 + 1.36053;
	float Fr = (1 - (0.542026*a2 + 0.303573*a) / a2_13) * (1 - pow(1 - NoV, 5 - 4 * a2) / a2_13) * ((-0.733996*a2*a + 1.50912*a2 - 1.16402*a) * pow(1 - NoV, 1 + rcp(39 * a2*a2 + 1)) + 1);
	//float Fr = ( 1 - 0.36 * a ) * ( 1 - pow( 1 - NoV, 5 - 4*a2 ) / a2_13 ) * ( -2.5 * Roughness * ( 1 - NoV ) + 1 );
	float Lm = (max(1 - 2 * a, 0) * (1 - Pow5(1 - NoL)) + min(2 * a, 1)) * (1 - 0.5*a * (NoL - 1)) * NoL;
	float Vd = (a2 / ((a2 + 0.09) * (1.31072 + 0.995584 * NoV))) * (1 - pow(1 - NoL, (1 - 0.3726732 * NoV * NoV) / (0.188566 + 0.38841 * NoV)));
	float Bp = Cosri < 0 ? 1.4 * NoV * NoL * Cosri : Cosri;
	float Lr = (21.0 / 20.0) * (1 - F0) * (Fr * Lm + Vd + Bp);
	return DiffuseColor / PI * Lr;
#else
	float a2_13 = a2 + 1.36053;
	float Fr = (1 - (0.542026*a2 + 0.303573*a) / a2_13) * (1 - pow(1 - NoV, 5 - 4 * a2) / a2_13) * ((-0.733996*a2*a + 1.50912*a2 - 1.16402*a) * pow(1 - NoV, 1 + rcp(39 * a2*a2 + 1)) + 1);
	float Lm = (max(1 - 2 * a, 0) * (1 - Pow5(1 - NoL)) + min(2 * a, 1)) * (1 - 0.5*a + 0.5*a * NoL);
	float Vd = (a2 / ((a2 + 0.09) * (1.31072 + 0.995584 * NoV))) * (1 - pow(1 - NoL, (1 - 0.3726732 * NoV * NoV) / (0.188566 + 0.38841 * NoV)));
	float Bp = Cosri < 0 ? 1.4 * NoV * Cosri : Cosri / max(NoL, 1e-8);
	float Lr = (21.0 / 20.0) * (1 - F0) * (Fr * Lm + Vd + Bp);
	return DiffuseColor / PI * Lr;
#endif
}

// [Blinn 1977, "Models of light reflection for computer synthesized pictures"]
float D_Blinn(float a2, float NoH)
{
	float n = 2 / a2 - 2;
	return (n + 2) / (2 * PI) * PhongShadingPow(NoH, n);		// 1 mad, 1 exp, 1 mul, 1 log
}

// [Beckmann 1963, "The scattering of electromagnetic waves from rough surfaces"]
float D_Beckmann(float a2, float NoH)
{
	float NoH2 = NoH * NoH;
	return exp((NoH2 - 1) / (a2 * NoH2)) / (PI * a2 * NoH2 * NoH2);
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
	float d = (NoH * a2 - NoH) * NoH + 1;	// 2 mad
	return a2 / (PI*d*d);					// 4 mul, 1 rcp
}

// Anisotropic GGX
// [Burley 2012, "Physically-Based Shading at Disney"]
float D_GGXaniso(float ax, float ay, float NoH, float3 H, float3 X, float3 Y)
{
	float XoH = dot(X, H);
	float YoH = dot(Y, H);
	float d = XoH * XoH / (ax*ax) + YoH * YoH / (ay*ay) + NoH * NoH;
	return 1 / (PI * ax*ay * d*d);
}

// [Neumann et al. 1999, "Compact metallic reflectance models"]
float Vis_Neumann(float NoV, float NoL)
{
	return 1 / (4 * max(NoL, NoV));
}

// [Kelemen 2001, "A microfacet based coupled specular-matte brdf model with importance sampling"]
float Vis_Kelemen(float VoH)
{
	// constant to prevent NaN
	return rcp(4 * VoH * VoH + 1e-5);
}

// Tuned to match behavior of Vis_Smith
// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float Vis_Schlick(float a2, float NoV, float NoL)
{
	float k = sqrt(a2) * 0.5;
	float Vis_SchlickV = NoV * (1 - k) + k;
	float Vis_SchlickL = NoL * (1 - k) + k;
	return 0.25 / (Vis_SchlickV * Vis_SchlickL);
}

// Smith term for GGX
// [Smith 1967, "Geometrical shadowing of a random rough surface"]
float Vis_Smith(float a2, float NoV, float NoL)
{
	float Vis_SmithV = NoV + sqrt(NoV * (NoV - NoV * a2) + a2);
	float Vis_SmithL = NoL + sqrt(NoL * (NoL - NoL * a2) + a2);
	return rcp(Vis_SmithV * Vis_SmithL);
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
	float a = sqrt(a2);
	float Vis_SmithV = NoL * (NoV * (1 - a) + a);
	float Vis_SmithL = NoV * (NoL * (1 - a) + a);
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick(float3 SpecularColor, float VoH)
{
	float Fc = Pow5(1 - VoH);					// 1 sub, 3 mul
	//return Fc + (1 - Fc) * SpecularColor;		// 1 add, 3 mad

	// Anything less than 2% is physically impossible and is instead considered to be shadowing
	return saturate(50.0 * SpecularColor.g) * Fc + (1 - Fc) * SpecularColor;

}