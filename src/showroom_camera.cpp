// FM4 USA Rev 1 showroom update, retained from its generated translation.
// Only the camera queue time step at 0x824BDCE4 is changed.
// Uses elapsed seconds instead of a fixed 1/60 s, as the FM4 reference project
// does in Fm4ShowroomCameraStep (i2kiselev/fm4-static-recomp).
#include "fm4_funcs.175.h"
#include "showroom_timing.h"

extern "C" void sub_824BDCA0(PPCContext& __restrict ctx, uint8_t* base) {
	REX_FUNC_PROLOGUE();
	PPCRegister temp{};
	uint32_t ea{};
	// mflr r12
	ctx.r12.u64 = ctx.lr;
	// stw r12,-8(r1)
	REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
	// std r30,-24(r1)
	REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
	// std r31,-16(r1)
	REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
	// li r12,-48
	ctx.r12.s64 = -48;
	// stvx128 v127,r1,r12
	ea = (ctx.r1.u32 + ctx.r12.u32) & ~0xF;
	simde_mm_store_si128((simde__m128i*)REX_RAW_ADDR(ea), simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)ctx.v127.u8), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
	// stwu r1,-304(r1)
	ea = -304 + ctx.r1.u32;
	REX_STORE_U32(ea, ctx.r1.u32);
	ctx.r1.u32 = ea;
	// mr r31,r3
	ctx.r31.u64 = ctx.r3.u64;
	// lwz r3,288(r3)
	ctx.r3.u64 = REX_LOAD_U32(ctx.r3.u32 + 288);
	// lwz r11,0(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
	// lwz r11,20(r11)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r11.u32 + 20);
	// mtctr r11
	ctx.ctr.u64 = ctx.r11.u64;
	// bctrl 
	ctx.lr = 0x824BDCD4;
	REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
	// lis r11,-32256
	ctx.r11.s64 = -2113929216;
	// lwz r3,288(r31)
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 288);
	// li r5,0
	ctx.r5.s64 = 0;
	// lfs f1,5308(r11)
	ctx.fpscr.disableFlushMode();
	temp.u32 = REX_LOAD_U32(ctx.r11.u32 + 5308);
	ctx.f1.f64 = double(temp.f32);
	// Rev 1 equivalent of the reference hook at 0x824B9D84: 0x824BDCE4.
	static thread_local fm4::ShowroomClock camera_clock;
	ctx.f1.f64 = camera_clock.Step(ctx.r3.u32, fm4::ShowroomClock::Clock::now());
	// lwz r11,0(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
	// lwz r11,12(r11)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r11.u32 + 12);
	// mtctr r11
	ctx.ctr.u64 = ctx.r11.u64;
	// bctrl 
	ctx.lr = 0x824BDCF4;
	REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
	// lbz r11,301(r31)
	ctx.r11.u64 = REX_LOAD_U8(ctx.r31.u32 + 301);
	// cmplwi r11,0
	ctx.cr0.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
	// beq 0x824bdd88
	if (ctx.cr0.eq) goto loc_824BDD88;
	// lwz r3,288(r31)
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 288);
	// bl 0x82686238
	ctx.lr = 0x824BDD08;
	sub_82686238(ctx, base);
	// mr r30,r3
	ctx.r30.u64 = ctx.r3.u64;
	// lfs f0,320(r31)
	ctx.fpscr.disableFlushMode();
	temp.u32 = REX_LOAD_U32(ctx.r31.u32 + 320);
	ctx.f0.f64 = double(temp.f32);
	// lis r11,-32256
	ctx.r11.s64 = -2113929216;
	// addi r3,r1,144
	ctx.r3.s64 = ctx.r1.s64 + 144;
	// lfs f13,400(r30)
	temp.u32 = REX_LOAD_U32(ctx.r30.u32 + 400);
	ctx.f13.f64 = double(temp.f32);
	// lfs f3,3912(r11)
	temp.u32 = REX_LOAD_U32(ctx.r11.u32 + 3912);
	ctx.f3.f64 = double(temp.f32);
	// fadds f1,f13,f0
	ctx.f1.f64 = double(float(ctx.f13.f64 + ctx.f0.f64));
	// fmr f2,f3
	ctx.f2.f64 = ctx.f3.f64;
	// bl 0x82394b30
	ctx.lr = 0x824BDD2C;
	sub_82394B30(ctx, base);
	// li r11,384
	ctx.r11.s64 = 384;
	// li r10,304
	ctx.r10.s64 = 304;
	// addi r4,r1,144
	ctx.r4.s64 = ctx.r1.s64 + 144;
	// addi r3,r1,192
	ctx.r3.s64 = ctx.r1.s64 + 192;
	// lvx128 v63,r30,r11
	ea = (ctx.r30.u32 + ctx.r11.u32) & ~0xF;
	simde_mm_store_si128((simde__m128i*)ctx.v63.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea)), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
	// lvx128 v62,r31,r10
	ea = (ctx.r31.u32 + ctx.r10.u32) & ~0xF;
	simde_mm_store_si128((simde__m128i*)ctx.v62.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea)), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
	// vaddfp128 v1,v62,v63
	ctx.fpscr.enableFlushMode();
	simde_mm_store_ps(ctx.v1.f32, simde_mm_add_ps(simde_mm_load_ps(ctx.v62.f32), simde_mm_load_ps(ctx.v63.f32)));
	// bl 0x8235e890
	ctx.lr = 0x824BDD4C;
	sub_8235E890(ctx, base);
	// lis r11,-32256
	ctx.r11.s64 = -2113929216;
	// lfs f2,404(r30)
	ctx.fpscr.disableFlushMode();
	temp.u32 = REX_LOAD_U32(ctx.r30.u32 + 404);
	ctx.f2.f64 = double(temp.f32);
	// ld r4,192(r1)
	ctx.r4.u64 = REX_LOAD_U64(ctx.r1.u32 + 192);
	// fmr f3,f2
	ctx.f3.f64 = ctx.f2.f64;
	// ld r5,200(r1)
	ctx.r5.u64 = REX_LOAD_U64(ctx.r1.u32 + 200);
	// lwz r3,292(r31)
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 292);
	// ld r6,208(r1)
	ctx.r6.u64 = REX_LOAD_U64(ctx.r1.u32 + 208);
	// lfs f1,3892(r11)
	temp.u32 = REX_LOAD_U32(ctx.r11.u32 + 3892);
	ctx.f1.f64 = double(temp.f32);
	// ld r7,216(r1)
	ctx.r7.u64 = REX_LOAD_U64(ctx.r1.u32 + 216);
	// ld r8,224(r1)
	ctx.r8.u64 = REX_LOAD_U64(ctx.r1.u32 + 224);
	// ld r9,232(r1)
	ctx.r9.u64 = REX_LOAD_U64(ctx.r1.u32 + 232);
	// ld r10,240(r1)
	ctx.r10.u64 = REX_LOAD_U64(ctx.r1.u32 + 240);
	// ld r11,248(r1)
	ctx.r11.u64 = REX_LOAD_U64(ctx.r1.u32 + 248);
	// std r11,88(r1)
	REX_STORE_U64(ctx.r1.u32 + 88, ctx.r11.u64);
	// bl 0x824b9b30
	ctx.lr = 0x824BDD88;
	sub_824B9B30(ctx, base);
loc_824BDD88:
	// lwz r4,288(r31)
	ctx.r4.u64 = REX_LOAD_U32(ctx.r31.u32 + 288);
	// lis r11,-32036
	ctx.r11.s64 = -2099511296;
	// addi r3,r1,128
	ctx.r3.s64 = ctx.r1.s64 + 128;
	// lwz r10,0(r4)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);
	// lwz r30,14384(r11)
	ctx.r30.u64 = REX_LOAD_U32(ctx.r11.u32 + 14384);
	// lwz r11,44(r10)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r10.u32 + 44);
	// mtctr r11
	ctx.ctr.u64 = ctx.r11.u64;
	// bctrl 
	ctx.lr = 0x824BDDA8;
	REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
	// mr r11,r3
	ctx.r11.u64 = ctx.r3.u64;
	// mr r3,r30
	ctx.r3.u64 = ctx.r30.u64;
	// lvx128 v127,r0,r11
	ea = (ctx.r11.u32) & ~0xF;
	simde_mm_store_si128((simde__m128i*)ctx.v127.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea)), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
	// bl 0x82ab06b8
	ctx.lr = 0x824BDDB8;
	sub_82AB06B8(ctx, base);
	// vor128 v1,v127,v127
	simde_mm_store_si128((simde__m128i*)ctx.v1.u8, simde_mm_load_si128((simde__m128i*)ctx.v127.u8));
	// bl 0x8239e1e8
	ctx.lr = 0x824BDDC0;
	sub_8239E1E8(ctx, base);
	// lwz r3,288(r31)
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 288);
	// bl 0x826d8e80
	ctx.lr = 0x824BDDC8;
	sub_826D8E80(ctx, base);
	// lwz r11,296(r31)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r31.u32 + 296);
	// stw r3,296(r31)
	REX_STORE_U32(ctx.r31.u32 + 296, ctx.r3.u32);
	// subf r11,r3,r11
	ctx.r11.u64 = ctx.r11.u64 - ctx.r3.u64;
	// addic r10,r11,-1
	ctx.xer.ca = ctx.r11.u32 > 0;
	ctx.r10.s64 = ctx.r11.s64 + -1;
	// subfe r11,r10,r11
	temp.u8 = (~ctx.r10.u32 + ctx.r11.u32 < ~ctx.r10.u32) | (~ctx.r10.u32 + ctx.r11.u32 + ctx.xer.ca < ctx.xer.ca);
	ctx.r11.u64 = ~ctx.r10.u64 + ctx.r11.u64 + ctx.xer.ca;
	ctx.xer.ca = temp.u8;
	// stb r11,300(r31)
	REX_STORE_U8(ctx.r31.u32 + 300, ctx.r11.u8);
	// addi r1,r1,304
	ctx.r1.s64 = ctx.r1.s64 + 304;
	// lwz r12,-8(r1)
	ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
	// mtlr r12
	ctx.lr = ctx.r12.u64;
	// li r0,-48
	ctx.r0.s64 = -48;
	// lvx128 v127,r1,r0
	ea = (ctx.r1.u32 + ctx.r0.u32) & ~0xF;
	simde_mm_store_si128((simde__m128i*)ctx.v127.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea)), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
	// ld r30,-24(r1)
	ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
	// ld r31,-16(r1)
	ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
	// blr 
	return;
}

