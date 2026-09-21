#!/usr/bin/env python3
"""Regression checks for scientific data and presentation geometry."""
import math
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

import render as film


class ExplainerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.e = film.Evidence(film.ROOT / 'build/reality-probe-explainer/replay')

    def test_trilinear_interpolation_preserves_every_solver_node(self):
        for direction in range(4):
            for model in (0,1,2):
                actual=self.e.field(direction,model,60,self.e.rest)
                np.testing.assert_allclose(actual,self.e.disp[direction,model,60],atol=1e-15)

    def test_temporal_interpolation_does_not_resimulate(self):
        actual=self.e.displacement(0,2,12.5)
        np.testing.assert_allclose(actual,(self.e.disp[0,2,12]+self.e.disp[0,2,13])/2,atol=1e-15)

    def test_raw_response_is_not_standardized_evidence(self):
        s=self.e.summary['px']
        raw_ratio=float(s['repair_raw_rms_to_truth_m'])/float(s['baseline_raw_rms_to_truth_m'])
        aggregate=self.e.ofc['force_to_dcs_median_abs_z_ratio']
        hero_ratio=abs(self.e.hero['force_progress_z']/self.e.hero['dcs_progress_z'])
        self.assertTrue(12 < raw_ratio < 12.1)
        self.assertTrue(11.45 < aggregate < 11.46)
        self.assertTrue(11.70 < hero_ratio < 11.71)
        self.assertFalse(math.isclose(aggregate,hero_ratio))

    def test_corrupted_signal_fails_closed(self):
        old=self.e.rows[0]['force_progress_z']
        try:
            self.e.rows[0]['force_progress_z']=2.01
            with self.assertRaises(AssertionError):self.e.validate()
        finally:self.e.rows[0]['force_progress_z']=old

    def test_shared_normalization_contains_all_directions(self):
        self.assertEqual(self.e.residual_max,float(self.e.residual[:,-1].max()))
        self.assertLess(self.e.residual_max,0.00014)

    def test_every_half_second_has_valid_svg_and_title_safe_text(self):
        ns={'s':'http://www.w3.org/2000/svg'}
        for frame in range(int(film.CFG['duration_s']*2)):
            t=frame/2
            svg=film.render_frame(self.e,t,vector=True).svg()
            self.assertNotIn('nan',svg)
            root=ET.fromstring(svg)
            for el in root.findall('s:text',ns):
                content=el.text or ''
                f=film.font(round(float(el.get('font-size'))),el.get('font-family')=='Georgia',el.get('font-weight')=='700')
                width=f.getlength(content)
                x=float(el.get('x'));anchor=el.get('text-anchor')
                left=x-(width/2 if anchor=='middle' else width if anchor=='end' else 0)
                self.assertGreaterEqual(left,30,msg=f'{t}s: {content}')
                self.assertLessEqual(left+width,1570,msg=f'{t}s: {content}')


if __name__=='__main__':unittest.main(verbosity=2)
