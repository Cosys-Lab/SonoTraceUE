// By Wouter Jansen & Jan Steckel, Cosys-Lab, University of Antwerp. See the LICENSE file for details. 
#pragma once

#include "CoreMinimal.h"

// Generated with the following Matlab code:
//
// numColors = 255;
// parulaMap = parula(numColors); 
// turboMap = turbo(numColors);
// jetMap = jet(numColors);   
// hotMap = hot(numColors);       
// parulaMapUint8 = uint8(parulaMap * 255);
// turboMapUint8 = uint8(turboMap * 255);
// jetMapUint8 = uint8(jetMap * 255);
// hotMapUint8 = uint8(hotMap * 255);


UENUM(BlueprintType)
enum class ESonoTraceUEColorMapEnum : uint8
{
	Parula UMETA(DisplayName = "Parula"),
	Turbo UMETA(DisplayName = "Turbo"),
	Jet UMETA(DisplayName = "Jet"),
	Hot UMETA(DisplayName = "Hot"),
};

class FColorMapSelector
{
public:
	static const TArray<FColor>& GetColorMap(const ESonoTraceUEColorMapEnum ColorMapType)
	{
		switch (ColorMapType)
		{
		case ESonoTraceUEColorMapEnum::Parula:
			return ParulaColorMap;
		case ESonoTraceUEColorMapEnum::Turbo:
			return TurboColorMap;
		case ESonoTraceUEColorMapEnum::Jet:
			return JetColorMap;
		case ESonoTraceUEColorMapEnum::Hot:
			return HotColorMap;
		default:
			return ParulaColorMap;
		}
	}
	
private:	

	inline static TArray<FColor> ParulaColorMap = {
		FColor(62, 38, 168), FColor(62, 39, 172), FColor(63, 40, 175), FColor(63, 41, 178),
	    FColor(64, 42, 180), FColor(64, 43, 183), FColor(65, 44, 186), FColor(65, 45, 189),
	    FColor(66, 46, 191), FColor(66, 47, 194), FColor(67, 48, 197), FColor(67, 49, 200),
	    FColor(67, 50, 203), FColor(68, 51, 205), FColor(68, 52, 208), FColor(69, 53, 210),
	    FColor(69, 55, 213), FColor(69, 56, 215), FColor(70, 57, 218), FColor(70, 58, 220),
	    FColor(70, 60, 222), FColor(70, 61, 224), FColor(71, 62, 226), FColor(71, 64, 227),
	    FColor(71, 65, 229), FColor(71, 66, 231), FColor(71, 68, 232), FColor(71, 69, 233),
	    FColor(71, 71, 235), FColor(72, 72, 236), FColor(72, 73, 237), FColor(72, 75, 239),
	    FColor(72, 76, 240), FColor(72, 78, 241), FColor(72, 79, 242), FColor(72, 81, 243),
	    FColor(72, 82, 244), FColor(72, 83, 245), FColor(72, 85, 246), FColor(71, 86, 247),
	    FColor(71, 87, 248), FColor(71, 89, 248), FColor(71, 90, 249), FColor(71, 92, 250),
	    FColor(70, 93, 250), FColor(70, 94, 251), FColor(70, 96, 252), FColor(69, 97, 252),
	    FColor(69, 99, 252), FColor(69, 100, 253), FColor(68, 102, 253), FColor(67, 103, 253),
	    FColor(67, 105, 254), FColor(66, 106, 254), FColor(65, 107, 254), FColor(64, 109, 254),
	    FColor(63, 110, 255), FColor(61, 112, 255), FColor(60, 113, 255), FColor(59, 115, 255),
	    FColor(57, 117, 255), FColor(55, 118, 254), FColor(54, 120, 254), FColor(52, 121, 253),
	    FColor(51, 123, 252), FColor(49, 124, 252), FColor(48, 126, 251), FColor(48, 127, 251),
	    FColor(47, 129, 250), FColor(47, 130, 250), FColor(46, 131, 249), FColor(46, 133, 248),
	    FColor(46, 134, 247), FColor(45, 135, 246), FColor(45, 137, 246), FColor(45, 138, 245),
	    FColor(45, 139, 243), FColor(45, 141, 242), FColor(45, 142, 241), FColor(44, 143, 240),
	    FColor(44, 145, 239), FColor(43, 146, 238), FColor(42, 147, 237), FColor(41, 148, 237),
	    FColor(40, 150, 236), FColor(39, 151, 235), FColor(38, 152, 234), FColor(38, 153, 233),
	    FColor(37, 155, 232), FColor(37, 156, 231), FColor(37, 157, 231), FColor(36, 158, 230),
	    FColor(36, 159, 229), FColor(35, 160, 229), FColor(34, 162, 228), FColor(33, 163, 228),
	    FColor(33, 164, 227), FColor(32, 165, 226), FColor(31, 166, 226), FColor(30, 167, 225),
	    FColor(29, 168, 224), FColor(28, 169, 223), FColor(27, 170, 222), FColor(26, 172, 221),
	    FColor(25, 173, 220), FColor(24, 174, 219), FColor(23, 175, 218), FColor(21, 175, 217),
	    FColor(19, 176, 215), FColor(17, 177, 214), FColor(15, 178, 212), FColor(13, 179, 211),
	    FColor(10, 180, 210), FColor(7, 181, 208), FColor(5, 181, 206), FColor(3, 182, 205),
	    FColor(2, 183, 203), FColor(1, 184, 202), FColor(0, 184, 200), FColor(0, 185, 199),
	    FColor(1, 186, 197), FColor(2, 186, 195), FColor(3, 187, 194), FColor(5, 188, 192),
	    FColor(8, 188, 190), FColor(11, 189, 189), FColor(15, 190, 187), FColor(18, 190, 185),
	    FColor(21, 191, 183), FColor(25, 191, 182), FColor(28, 192, 180), FColor(31, 192, 178),
	    FColor(33, 193, 176), FColor(36, 193, 175), FColor(38, 194, 173), FColor(40, 194, 171),
	    FColor(42, 195, 169), FColor(44, 196, 167), FColor(45, 196, 165), FColor(47, 197, 164),
	    FColor(48, 197, 162), FColor(49, 198, 160), FColor(51, 198, 158), FColor(52, 199, 156),
	    FColor(53, 199, 153), FColor(55, 200, 151), FColor(57, 200, 149), FColor(58, 201, 147),
	    FColor(60, 201, 145), FColor(63, 202, 142), FColor(65, 202, 140), FColor(68, 203, 138),
	    FColor(70, 203, 135), FColor(73, 203, 133), FColor(77, 204, 130), FColor(80, 204, 128),
	    FColor(83, 204, 125), FColor(86, 204, 123), FColor(89, 204, 120), FColor(92, 204, 117),
	    FColor(96, 205, 115), FColor(99, 205, 112), FColor(102, 205, 109), FColor(105, 205, 106),
	    FColor(109, 205, 103), FColor(113, 205, 101), FColor(116, 205, 98), FColor(120, 204, 95),
	    FColor(124, 204, 92), FColor(127, 204, 90), FColor(131, 204, 87), FColor(135, 203, 84),
	    FColor(138, 203, 81), FColor(142, 203, 79), FColor(145, 202, 76), FColor(149, 202, 73),
	    FColor(153, 201, 70), FColor(156, 201, 68), FColor(160, 201, 65), FColor(163, 200, 63),
	    FColor(166, 200, 60), FColor(170, 199, 58), FColor(173, 198, 56), FColor(177, 198, 53),
	    FColor(180, 197, 51), FColor(183, 197, 50), FColor(186, 196, 48), FColor(190, 195, 46),
	    FColor(193, 195, 44), FColor(196, 194, 43), FColor(199, 193, 41), FColor(202, 193, 40),
	    FColor(205, 192, 40), FColor(208, 192, 39), FColor(211, 191, 39), FColor(213, 190, 39),
	    FColor(216, 190, 40), FColor(219, 189, 40), FColor(221, 189, 41), FColor(224, 188, 42),
	    FColor(226, 188, 43), FColor(229, 187, 44), FColor(231, 187, 46), FColor(234, 186, 48),
	    FColor(236, 186, 50), FColor(238, 186, 52), FColor(240, 186, 54), FColor(242, 186, 57),
	    FColor(245, 186, 59), FColor(247, 186, 60), FColor(249, 186, 61), FColor(251, 187, 62),
	    FColor(252, 188, 62), FColor(253, 189, 61), FColor(254, 190, 60), FColor(254, 191, 59),
	    FColor(254, 193, 58), FColor(254, 194, 57), FColor(254, 196, 56), FColor(254, 197, 55),
	    FColor(254, 199, 54), FColor(254, 200, 52), FColor(254, 202, 51), FColor(253, 203, 50),
	    FColor(253, 205, 50), FColor(253, 206, 49), FColor(252, 208, 48), FColor(252, 209, 47),
	    FColor(251, 211, 46), FColor(250, 213, 46), FColor(249, 214, 45), FColor(249, 216, 44),
	    FColor(248, 217, 43), FColor(247, 219, 43), FColor(247, 221, 42), FColor(246, 222, 41),
	    FColor(246, 224, 40), FColor(245, 225, 40), FColor(245, 227, 39), FColor(245, 228, 38),
	    FColor(245, 230, 38), FColor(245, 232, 37), FColor(245, 233, 36), FColor(245, 235, 35),
	    FColor(245, 236, 34), FColor(245, 238, 33), FColor(246, 239, 32), FColor(246, 241, 31),
	    FColor(246, 242, 30), FColor(247, 244, 28), FColor(247, 245, 27), FColor(248, 246, 26),
	    FColor(248, 248, 24), FColor(249, 249, 22), FColor(249, 251, 21)
	};

	inline static TArray<FColor> TurboColorMap = {
	    FColor(48, 18, 59), FColor(50, 21, 67), FColor(51, 24, 74), FColor(52, 27, 81),
	    FColor(53, 30, 88), FColor(54, 33, 95), FColor(55, 36, 102), FColor(56, 39, 109),
	    FColor(57, 42, 115), FColor(58, 45, 122), FColor(59, 48, 128), FColor(60, 50, 134),
	    FColor(61, 53, 140), FColor(62, 56, 145), FColor(63, 59, 151), FColor(63, 62, 157),
	    FColor(64, 65, 162), FColor(65, 67, 167), FColor(65, 70, 172), FColor(66, 73, 177),
	    FColor(67, 76, 182), FColor(67, 78, 186), FColor(68, 81, 191), FColor(68, 84, 195),
	    FColor(68, 87, 199), FColor(69, 89, 203), FColor(69, 92, 207), FColor(69, 95, 211),
	    FColor(70, 97, 215), FColor(70, 100, 218), FColor(70, 102, 221), FColor(70, 105, 225),
	    FColor(70, 108, 228), FColor(71, 110, 231), FColor(71, 113, 233), FColor(71, 115, 236),
	    FColor(71, 118, 238), FColor(71, 121, 241), FColor(70, 123, 243), FColor(70, 126, 245),
	    FColor(70, 128, 247), FColor(70, 131, 248), FColor(70, 133, 250), FColor(69, 136, 251),
	    FColor(69, 138, 252), FColor(68, 141, 253), FColor(68, 143, 254), FColor(67, 146, 254),
	    FColor(66, 148, 255), FColor(65, 151, 255), FColor(63, 153, 255), FColor(62, 156, 254),
	    FColor(61, 158, 254), FColor(59, 161, 253), FColor(57, 164, 252), FColor(56, 166, 251),
	    FColor(54, 169, 249), FColor(52, 171, 248), FColor(51, 174, 247), FColor(49, 176, 245),
	    FColor(47, 179, 243), FColor(45, 181, 241), FColor(43, 183, 239), FColor(42, 186, 237),
	    FColor(40, 188, 235), FColor(38, 190, 233), FColor(37, 193, 230), FColor(35, 195, 228),
	    FColor(33, 197, 225), FColor(32, 200, 223), FColor(31, 202, 220), FColor(29, 204, 217),
	    FColor(28, 206, 215), FColor(27, 208, 212), FColor(26, 210, 209), FColor(25, 212, 207),
	    FColor(25, 214, 204), FColor(24, 216, 201), FColor(24, 218, 199), FColor(24, 220, 196),
	    FColor(24, 221, 194), FColor(24, 223, 191), FColor(24, 225, 189), FColor(25, 226, 186),
	    FColor(26, 228, 184), FColor(27, 229, 182), FColor(28, 230, 179), FColor(30, 232, 177),
	    FColor(31, 233, 174), FColor(33, 234, 171), FColor(35, 236, 169), FColor(38, 237, 166),
	    FColor(40, 238, 163), FColor(43, 239, 160), FColor(45, 240, 157), FColor(48, 241, 154),
	    FColor(51, 242, 150), FColor(54, 243, 147), FColor(58, 244, 144), FColor(61, 245, 141),
	    FColor(65, 246, 137), FColor(68, 247, 134), FColor(72, 248, 130), FColor(76, 249, 127),
	    FColor(79, 249, 124), FColor(83, 250, 120), FColor(87, 251, 117), FColor(91, 251, 113),
	    FColor(95, 252, 110), FColor(99, 252, 107), FColor(103, 253, 103), FColor(107, 253, 100),
	    FColor(111, 254, 97), FColor(115, 254, 94), FColor(119, 254, 91), FColor(123, 254, 88),
	    FColor(126, 255, 85), FColor(130, 255, 82), FColor(134, 255, 79), FColor(137, 255, 77),
	    FColor(141, 255, 74), FColor(144, 255, 72), FColor(148, 254, 70), FColor(151, 254, 67),
	    FColor(154, 254, 65), FColor(157, 254, 64), FColor(160, 253, 62), FColor(163, 253, 60),
	    FColor(165, 252, 59), FColor(168, 252, 58), FColor(171, 251, 57), FColor(173, 250, 56),
	    FColor(176, 249, 55), FColor(179, 248, 54), FColor(181, 247, 53), FColor(184, 246, 53),
	    FColor(187, 245, 52), FColor(189, 244, 52), FColor(192, 243, 52), FColor(194, 242, 52),
	    FColor(197, 241, 52), FColor(199, 239, 52), FColor(202, 238, 52), FColor(204, 236, 52),
	    FColor(207, 235, 52), FColor(209, 233, 53), FColor(211, 232, 53), FColor(214, 230, 53),
	    FColor(216, 228, 54), FColor(218, 227, 54), FColor(220, 225, 54), FColor(222, 223, 55),
	    FColor(224, 221, 55), FColor(226, 220, 56), FColor(228, 218, 56), FColor(230, 216, 56),
	    FColor(232, 214, 57), FColor(234, 212, 57), FColor(236, 210, 57), FColor(237, 208, 58),
	    FColor(239, 206, 58), FColor(240, 204, 58), FColor(242, 202, 58), FColor(243, 200, 58),
	    FColor(244, 198, 58), FColor(246, 195, 58), FColor(247, 193, 58), FColor(248, 191, 58),
	    FColor(249, 189, 57), FColor(250, 187, 57), FColor(250, 185, 56), FColor(251, 182, 55),
	    FColor(252, 180, 55), FColor(252, 178, 54), FColor(253, 175, 53), FColor(253, 173, 52),
	    FColor(253, 170, 51), FColor(254, 167, 50), FColor(254, 165, 49), FColor(254, 162, 48),
	    FColor(254, 159, 47), FColor(254, 156, 46), FColor(254, 153, 45), FColor(254, 151, 43),
	    FColor(254, 148, 42), FColor(254, 145, 41), FColor(253, 142, 40), FColor(253, 139, 38),
	    FColor(253, 136, 37), FColor(252, 133, 36), FColor(252, 129, 34), FColor(251, 126, 33),
	    FColor(250, 123, 32), FColor(250, 120, 30), FColor(249, 117, 29), FColor(248, 114, 28),
	    FColor(247, 111, 27), FColor(246, 108, 25), FColor(245, 105, 24), FColor(245, 103, 23),
	    FColor(243, 100, 22), FColor(242, 97, 20), FColor(241, 94, 19), FColor(240, 91, 18),
	    FColor(239, 89, 17), FColor(238, 86, 16), FColor(236, 83, 15), FColor(235, 81, 14),
	    FColor(234, 78, 13), FColor(232, 76, 12), FColor(231, 74, 12), FColor(229, 71, 11),
	    FColor(228, 69, 10), FColor(226, 67, 10), FColor(225, 65, 9), FColor(223, 63, 8),
	    FColor(221, 61, 8), FColor(220, 59, 7), FColor(218, 57, 7), FColor(216, 55, 6),
	    FColor(214, 53, 6), FColor(212, 51, 5), FColor(210, 49, 5), FColor(208, 47, 5),
	    FColor(206, 45, 4), FColor(204, 44, 4), FColor(202, 42, 4), FColor(200, 40, 3),
	    FColor(198, 38, 3), FColor(195, 37, 3), FColor(193, 35, 2), FColor(190, 34, 2),
	    FColor(188, 32, 2), FColor(186, 30, 2), FColor(183, 29, 2), FColor(180, 27, 1),
	    FColor(178, 26, 1), FColor(175, 24, 1), FColor(172, 23, 1), FColor(170, 22, 1),
	    FColor(167, 20, 1), FColor(164, 19, 1), FColor(161, 18, 1), FColor(158, 16, 1),
	    FColor(155, 15, 1), FColor(152, 14, 1), FColor(149, 13, 1), FColor(146, 11, 1),
	    FColor(142, 10, 1), FColor(139, 9, 2), FColor(136, 8, 2), FColor(133, 7, 2),
	    FColor(129, 6, 2), FColor(126, 5, 2), FColor(122, 4, 3)
	};

	inline static TArray<FColor> JetColorMap = {
		    FColor(0, 0, 131), FColor(0, 0, 135), FColor(0, 0, 139), FColor(0, 0, 143), FColor(0, 0, 147),
	    FColor(0, 0, 151), FColor(0, 0, 155), FColor(0, 0, 159), FColor(0, 0, 163), FColor(0, 0, 167),
	    FColor(0, 0, 171), FColor(0, 0, 175), FColor(0, 0, 179), FColor(0, 0, 183), FColor(0, 0, 187),
	    FColor(0, 0, 191), FColor(0, 0, 195), FColor(0, 0, 199), FColor(0, 0, 203), FColor(0, 0, 207),
	    FColor(0, 0, 211), FColor(0, 0, 215), FColor(0, 0, 219), FColor(0, 0, 223), FColor(0, 0, 227),
	    FColor(0, 0, 231), FColor(0, 0, 235), FColor(0, 0, 239), FColor(0, 0, 243), FColor(0, 0, 247),
	    FColor(0, 0, 251), FColor(0, 0, 255), FColor(0, 4, 255), FColor(0, 8, 255), FColor(0, 12, 255),
	    FColor(0, 16, 255), FColor(0, 20, 255), FColor(0, 24, 255), FColor(0, 28, 255), FColor(0, 32, 255),
	    FColor(0, 36, 255), FColor(0, 40, 255), FColor(0, 44, 255), FColor(0, 48, 255), FColor(0, 52, 255),
	    FColor(0, 56, 255), FColor(0, 60, 255), FColor(0, 64, 255), FColor(0, 68, 255), FColor(0, 72, 255),
	    FColor(0, 76, 255), FColor(0, 80, 255), FColor(0, 84, 255), FColor(0, 88, 255), FColor(0, 92, 255),
	    FColor(0, 96, 255), FColor(0, 100, 255), FColor(0, 104, 255), FColor(0, 108, 255), FColor(0, 112, 255),
	    FColor(0, 116, 255), FColor(0, 120, 255), FColor(0, 124, 255), FColor(0, 128, 255), FColor(0, 131, 255),
	    FColor(0, 135, 255), FColor(0, 139, 255), FColor(0, 143, 255), FColor(0, 147, 255), FColor(0, 151, 255),
	    FColor(0, 155, 255), FColor(0, 159, 255), FColor(0, 163, 255), FColor(0, 167, 255), FColor(0, 171, 255),
	    FColor(0, 175, 255), FColor(0, 179, 255), FColor(0, 183, 255), FColor(0, 187, 255), FColor(0, 191, 255),
	    FColor(0, 195, 255), FColor(0, 199, 255), FColor(0, 203, 255), FColor(0, 207, 255), FColor(0, 211, 255),
	    FColor(0, 215, 255), FColor(0, 219, 255), FColor(0, 223, 255), FColor(0, 227, 255), FColor(0, 231, 255),
	    FColor(0, 235, 255), FColor(0, 239, 255), FColor(0, 243, 255), FColor(0, 247, 255), FColor(0, 251, 255),
	    FColor(0, 255, 255), FColor(4, 255, 251), FColor(8, 255, 247), FColor(12, 255, 243), FColor(16, 255, 239),
	    FColor(20, 255, 235), FColor(24, 255, 231), FColor(28, 255, 227), FColor(32, 255, 223), FColor(36, 255, 219),
	    FColor(40, 255, 215), FColor(44, 255, 211), FColor(48, 255, 207), FColor(52, 255, 203), FColor(56, 255, 199),
	    FColor(60, 255, 195), FColor(64, 255, 191), FColor(68, 255, 187), FColor(72, 255, 183), FColor(76, 255, 179),
	    FColor(80, 255, 175), FColor(84, 255, 171), FColor(88, 255, 167), FColor(92, 255, 163), FColor(96, 255, 159),
	    FColor(100, 255, 155), FColor(104, 255, 151), FColor(108, 255, 147), FColor(112, 255, 143), FColor(116, 255, 139),
	    FColor(120, 255, 135), FColor(124, 255, 131), FColor(128, 255, 128), FColor(131, 255, 124), FColor(135, 255, 120),
	    FColor(139, 255, 116), FColor(143, 255, 112), FColor(147, 255, 108), FColor(151, 255, 104), FColor(155, 255, 100),
	    FColor(159, 255, 96), FColor(163, 255, 92), FColor(167, 255, 88), FColor(171, 255, 84), FColor(175, 255, 80),
	    FColor(179, 255, 76), FColor(183, 255, 72), FColor(187, 255, 68), FColor(191, 255, 64), FColor(195, 255, 60),
	    FColor(199, 255, 56), FColor(203, 255, 52), FColor(207, 255, 48), FColor(211, 255, 44), FColor(215, 255, 40),
	    FColor(219, 255, 36), FColor(223, 255, 32), FColor(227, 255, 28), FColor(231, 255, 24), FColor(235, 255, 20),
	    FColor(239, 255, 16), FColor(243, 255, 12), FColor(247, 255, 8), FColor(251, 255, 4), FColor(255, 255, 0),
	    FColor(255, 251, 0), FColor(255, 247, 0), FColor(255, 243, 0), FColor(255, 239, 0), FColor(255, 235, 0),
	    FColor(255, 231, 0), FColor(255, 227, 0), FColor(255, 223, 0), FColor(255, 219, 0), FColor(255, 215, 0),
	    FColor(255, 211, 0), FColor(255, 207, 0), FColor(255, 203, 0), FColor(255, 199, 0), FColor(255, 195, 0),
	    FColor(255, 191, 0), FColor(255, 187, 0), FColor(255, 183, 0), FColor(255, 179, 0), FColor(255, 175, 0),
	    FColor(255, 171, 0), FColor(255, 167, 0), FColor(255, 163, 0), FColor(255, 159, 0), FColor(255, 155, 0),
	    FColor(255, 151, 0), FColor(255, 147, 0), FColor(255, 143, 0), FColor(255, 139, 0), FColor(255, 135, 0),
	    FColor(255, 131, 0), FColor(255, 128, 0), FColor(255, 124, 0), FColor(255, 120, 0), FColor(255, 116, 0),
	    FColor(255, 112, 0), FColor(255, 108, 0), FColor(255, 104, 0), FColor(255, 100, 0), FColor(255, 96, 0),
	    FColor(255, 92, 0), FColor(255, 88, 0), FColor(255, 84, 0), FColor(255, 80, 0), FColor(255, 76, 0),
	    FColor(255, 72, 0), FColor(255, 68, 0), FColor(255, 64, 0), FColor(255, 60, 0), FColor(255, 56, 0),
	    FColor(255, 52, 0), FColor(255, 48, 0), FColor(255, 44, 0), FColor(255, 40, 0), FColor(255, 36, 0),
	    FColor(255, 32, 0), FColor(255, 28, 0), FColor(255, 24, 0), FColor(255, 20, 0), FColor(255, 16, 0),
	    FColor(255, 12, 0), FColor(255, 8, 0), FColor(255, 4, 0), FColor(255, 0, 0), FColor(251, 0, 0),
	    FColor(247, 0, 0), FColor(243, 0, 0), FColor(239, 0, 0), FColor(235, 0, 0), FColor(231, 0, 0),
	    FColor(227, 0, 0), FColor(223, 0, 0), FColor(219, 0, 0), FColor(215, 0, 0), FColor(211, 0, 0),
	    FColor(207, 0, 0), FColor(203, 0, 0), FColor(199, 0, 0), FColor(195, 0, 0), FColor(191, 0, 0),
	    FColor(187, 0, 0), FColor(183, 0, 0), FColor(179, 0, 0), FColor(175, 0, 0), FColor(171, 0, 0),
	    FColor(167, 0, 0), FColor(163, 0, 0), FColor(159, 0, 0), FColor(155, 0, 0), FColor(151, 0, 0),
	    FColor(147, 0, 0), FColor(143, 0, 0), FColor(139, 0, 0), FColor(135, 0, 0), FColor(131, 0, 0)
	};

	inline static TArray<FColor> HotColorMap = {
		FColor(3, 0, 0), FColor(5, 0, 0), FColor(8, 0, 0), FColor(11, 0, 0), FColor(13, 0, 0),
	    FColor(16, 0, 0), FColor(19, 0, 0), FColor(21, 0, 0), FColor(24, 0, 0), FColor(27, 0, 0),
	    FColor(30, 0, 0), FColor(32, 0, 0), FColor(35, 0, 0), FColor(38, 0, 0), FColor(40, 0, 0),
	    FColor(43, 0, 0), FColor(46, 0, 0), FColor(48, 0, 0), FColor(51, 0, 0), FColor(54, 0, 0),
	    FColor(56, 0, 0), FColor(59, 0, 0), FColor(62, 0, 0), FColor(64, 0, 0), FColor(67, 0, 0),
	    FColor(70, 0, 0), FColor(72, 0, 0), FColor(75, 0, 0), FColor(78, 0, 0), FColor(81, 0, 0),
	    FColor(83, 0, 0), FColor(86, 0, 0), FColor(89, 0, 0), FColor(91, 0, 0), FColor(94, 0, 0),
	    FColor(97, 0, 0), FColor(99, 0, 0), FColor(102, 0, 0), FColor(105, 0, 0), FColor(107, 0, 0),
	    FColor(110, 0, 0), FColor(113, 0, 0), FColor(115, 0, 0), FColor(118, 0, 0), FColor(121, 0, 0),
	    FColor(123, 0, 0), FColor(126, 0, 0), FColor(129, 0, 0), FColor(132, 0, 0), FColor(134, 0, 0),
	    FColor(137, 0, 0), FColor(140, 0, 0), FColor(142, 0, 0), FColor(145, 0, 0), FColor(148, 0, 0),
	    FColor(150, 0, 0), FColor(153, 0, 0), FColor(156, 0, 0), FColor(158, 0, 0), FColor(161, 0, 0),
	    FColor(164, 0, 0), FColor(166, 0, 0), FColor(169, 0, 0), FColor(172, 0, 0), FColor(174, 0, 0),
	    FColor(177, 0, 0), FColor(180, 0, 0), FColor(183, 0, 0), FColor(185, 0, 0), FColor(188, 0, 0),
	    FColor(191, 0, 0), FColor(193, 0, 0), FColor(196, 0, 0), FColor(199, 0, 0), FColor(201, 0, 0),
	    FColor(204, 0, 0), FColor(207, 0, 0), FColor(209, 0, 0), FColor(212, 0, 0), FColor(215, 0, 0),
	    FColor(217, 0, 0), FColor(220, 0, 0), FColor(223, 0, 0), FColor(225, 0, 0), FColor(228, 0, 0),
	    FColor(231, 0, 0), FColor(234, 0, 0), FColor(236, 0, 0), FColor(239, 0, 0), FColor(242, 0, 0),
	    FColor(244, 0, 0), FColor(247, 0, 0), FColor(250, 0, 0), FColor(252, 0, 0), FColor(255, 0, 0),
	    FColor(255, 3, 0), FColor(255, 5, 0), FColor(255, 8, 0), FColor(255, 11, 0), FColor(255, 13, 0),
	    FColor(255, 16, 0), FColor(255, 19, 0), FColor(255, 21, 0), FColor(255, 24, 0), FColor(255, 27, 0),
	    FColor(255, 30, 0), FColor(255, 32, 0), FColor(255, 35, 0), FColor(255, 38, 0), FColor(255, 40, 0),
	    FColor(255, 43, 0), FColor(255, 46, 0), FColor(255, 48, 0), FColor(255, 51, 0), FColor(255, 54, 0),
	    FColor(255, 56, 0), FColor(255, 59, 0), FColor(255, 62, 0), FColor(255, 64, 0), FColor(255, 67, 0),
	    FColor(255, 70, 0), FColor(255, 72, 0), FColor(255, 75, 0), FColor(255, 78, 0), FColor(255, 81, 0),
	    FColor(255, 83, 0), FColor(255, 86, 0), FColor(255, 89, 0), FColor(255, 91, 0), FColor(255, 94, 0),
	    FColor(255, 97, 0), FColor(255, 99, 0), FColor(255, 102, 0), FColor(255, 105, 0), FColor(255, 107, 0),
	    FColor(255, 110, 0), FColor(255, 113, 0), FColor(255, 115, 0), FColor(255, 118, 0), FColor(255, 121, 0),
	    FColor(255, 123, 0), FColor(255, 126, 0), FColor(255, 129, 0), FColor(255, 132, 0), FColor(255, 134, 0),
	    FColor(255, 137, 0), FColor(255, 140, 0), FColor(255, 142, 0), FColor(255, 145, 0), FColor(255, 148, 0),
	    FColor(255, 150, 0), FColor(255, 153, 0), FColor(255, 156, 0), FColor(255, 158, 0), FColor(255, 161, 0),
	    FColor(255, 164, 0), FColor(255, 166, 0), FColor(255, 169, 0), FColor(255, 172, 0), FColor(255, 174, 0),
	    FColor(255, 177, 0), FColor(255, 180, 0), FColor(255, 183, 0), FColor(255, 185, 0), FColor(255, 188, 0),
	    FColor(255, 191, 0), FColor(255, 193, 0), FColor(255, 196, 0), FColor(255, 199, 0), FColor(255, 201, 0),
	    FColor(255, 204, 0), FColor(255, 207, 0), FColor(255, 209, 0), FColor(255, 212, 0), FColor(255, 215, 0),
	    FColor(255, 217, 0), FColor(255, 220, 0), FColor(255, 223, 0), FColor(255, 225, 0), FColor(255, 228, 0),
	    FColor(255, 231, 0), FColor(255, 234, 0), FColor(255, 236, 0), FColor(255, 239, 0), FColor(255, 242, 0),
	    FColor(255, 244, 0), FColor(255, 247, 0), FColor(255, 250, 0), FColor(255, 252, 0), FColor(255, 255, 0),
	    FColor(255, 255, 4), FColor(255, 255, 8), FColor(255, 255, 12), FColor(255, 255, 16), FColor(255, 255, 20),
	    FColor(255, 255, 24), FColor(255, 255, 27), FColor(255, 255, 31), FColor(255, 255, 35), FColor(255, 255, 39),
	    FColor(255, 255, 43), FColor(255, 255, 47), FColor(255, 255, 51), FColor(255, 255, 55), FColor(255, 255, 59),
	    FColor(255, 255, 63), FColor(255, 255, 67), FColor(255, 255, 71), FColor(255, 255, 75), FColor(255, 255, 78),
	    FColor(255, 255, 82), FColor(255, 255, 86), FColor(255, 255, 90), FColor(255, 255, 94), FColor(255, 255, 98),
	    FColor(255, 255, 102), FColor(255, 255, 106), FColor(255, 255, 110), FColor(255, 255, 114), FColor(255, 255, 118),
	    FColor(255, 255, 122), FColor(255, 255, 126), FColor(255, 255, 129), FColor(255, 255, 133), FColor(255, 255, 137),
	    FColor(255, 255, 141), FColor(255, 255, 145), FColor(255, 255, 149), FColor(255, 255, 153), FColor(255, 255, 157),
	    FColor(255, 255, 161), FColor(255, 255, 165), FColor(255, 255, 169), FColor(255, 255, 173), FColor(255, 255, 177),
	    FColor(255, 255, 180), FColor(255, 255, 184), FColor(255, 255, 188), FColor(255, 255, 192), FColor(255, 255, 196),
	    FColor(255, 255, 200), FColor(255, 255, 204), FColor(255, 255, 208), FColor(255, 255, 212), FColor(255, 255, 216),
	    FColor(255, 255, 220), FColor(255, 255, 224), FColor(255, 255, 228), FColor(255, 255, 231), FColor(255, 255, 235),
	    FColor(255, 255, 239), FColor(255, 255, 243), FColor(255, 255, 247), FColor(255, 255, 251), FColor(255, 255, 255)
	};
};
