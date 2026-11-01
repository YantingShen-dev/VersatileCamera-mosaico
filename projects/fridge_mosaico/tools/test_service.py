import unittest
import io
import tempfile
from pathlib import Path
from unittest.mock import patch
from PIL import Image
from service import food_items, milli, name_checked, date_checked, upload, observations

class ValidationTests(unittest.TestCase):
    def test_drink_evidence(self):
        for label,expected in [('',4),('瑞幸',4),('冰美式',6)]:
            item=observations({'items':[dict(category='黑咖啡',brand='瑞幸',quantity=1,kind='drink',evidence='label',label_text=label)]})[0]
            self.assertEqual(item['evidence'],expected)
            self.assertEqual(item['name'],'瑞幸黑咖啡' if expected==6 else '饮品')
    def test_brand_and_category(self):
        for evidence,expected in [('label','奥利奥饼干'),('appearance','饼干')]:
            item=observations({'items':[dict(category='夹心饼干',brand='奥利奥',quantity=1,kind='food',evidence=evidence,label_text='奥利奥夹心饼干')]} )[0]
            self.assertEqual(item['name'],expected)
    def test_quantities(self):
        self.assertEqual(milli(1.125),1125)
        for value in [True,-1,'NaN','Infinity',100001,.0001]:
            with self.assertRaises(ValueError):milli(value)
    def test_duplicates(self):
        items=food_items({'items':[{'name':'西红柿','quantity':2},{'name':'西红柿','quantity':1.5}]})
        self.assertEqual(len(items),1)
        self.assertEqual(items[0]['quantity'],3500)
        self.assertEqual(set(items[0]),{'name','quantity','date'})
    def test_name_and_date(self):
        for name in ['', 'x\ny','x\ty','茄'*30]:
            with self.assertRaises(ValueError):name_checked(name)
        with self.assertRaises(ValueError):date_checked('2025-02-29')
        self.assertEqual(date_checked('2024-02-29'),'2024-02-29')
    def test_food_boundary(self):
        self.assertEqual(food_items({'items':[]}),[])
        with self.assertRaises(ValueError):food_items({'items':[{'name':'西红柿','quantity':0}]})
        with self.assertRaises(ValueError):food_items({'items':[{}]*25})

class PreviewTests(unittest.IsolatedAsyncioTestCase):
    async def test_preview_matches_recognition_crop(self):
        # Red edges must disappear from both images, not just the preview.
        image = Image.new('RGB', (800, 400), 'red')
        image.paste('green', (200, 0, 600, 400))
        raw = io.BytesIO(); image.save(raw, format='PNG')
        class Request:
            query = {}
            async def read(self): return raw.getvalue()
        with tempfile.TemporaryDirectory() as directory, patch('service.DATA', Path(directory)):
            await upload(Request())
            with Image.open(Path(directory) / 'capture.jpg') as capture:
                self.assertEqual(capture.size, (400, 400))
                self.assertGreater(capture.getpixel((0, 0))[1], 100)
                self.assertLess(capture.getpixel((0, 0))[0], 10)
            preview = (Path(directory) / 'preview.rgb565').read_bytes()
            self.assertEqual(len(preview), 480 * 480 * 2)
            self.assertEqual(preview[:2], (32 << 5).to_bytes(2, 'little'))


    async def test_live_preview_does_not_replace_shutter_photo(self):
        class Request:
            def __init__(self, color, preview):
                self.query = {'preview': '1'} if preview else {}
                raw = io.BytesIO(); Image.new('RGB', (480, 480), color).save(raw, format='PNG')
                self.raw = raw.getvalue()
            async def read(self): return self.raw
        with tempfile.TemporaryDirectory() as directory, patch('service.DATA', Path(directory)):
            await upload(Request('red', False))
            shutter = (Path(directory) / 'capture.jpg').read_bytes()
            await upload(Request('blue', True))
            self.assertEqual((Path(directory) / 'capture.jpg').read_bytes(), shutter)
            self.assertEqual((Path(directory) / 'preview.rgb565').read_bytes()[:2], (31).to_bytes(2, 'little'))

if __name__=='__main__':unittest.main()
