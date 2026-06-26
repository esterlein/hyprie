#pragma once


namespace hpr::io {


struct InputBinding
{
	bool orbit_rmb          {true};
	bool pan_mmb_shift_rmb  {true};

	float orbit_sensitivity {0.0025f};
	float pan_sensitivity   {0.0015f};
	float dolly_sensitivity {0.12f};
};

} // hpr::io

