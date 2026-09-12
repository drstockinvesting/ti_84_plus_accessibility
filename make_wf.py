"""Write API-format ComfyUI workflows for the material pass (run them via comfy-mcp run_workflow).

usage: make_wf.py            -> wf/img2img_d{dn}_s{seed}.json sweep + wf_img2img.json (first variant)
       make_wf.py up IMAGE   -> wf/upscale.json for an image already in ComfyUI/input/
"""
import json, os, sys

POS = ("studio product photograph of a graphing calculator, straight-on, "
       "matte molded plastic keys, soft even lighting")
NEG = "text, letters, numbers, logo, watermark, perspective, tilted, blur"


def img2img(denoise, seed, image="ti84_layout.png"):
    return {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "RealVisXL_V5.0_fp16.safetensors"}},
        "2": {"class_type": "LoadImage", "inputs": {"image": image}},
        "3": {"class_type": "VAEEncode", "inputs": {"pixels": ["2", 0], "vae": ["1", 2]}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"text": POS, "clip": ["1", 1]}},
        "5": {"class_type": "CLIPTextEncode", "inputs": {"text": NEG, "clip": ["1", 1]}},
        "6": {"class_type": "KSampler", "inputs": {
            "model": ["1", 0], "positive": ["4", 0], "negative": ["5", 0], "latent_image": ["3", 0],
            "seed": seed, "steps": 35, "cfg": 6.0, "sampler_name": "dpmpp_2m", "scheduler": "karras",
            "denoise": denoise}},
        "7": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["1", 2]}},
        "8": {"class_type": "SaveImage", "inputs": {"images": ["7", 0],
              "filename_prefix": f"ti84/diff_d{int(round(denoise * 100))}_s{seed}"}},
    }


def upscale(image):
    return {
        "1": {"class_type": "LoadImage", "inputs": {"image": image}},
        "2": {"class_type": "UpscaleModelLoader", "inputs": {"model_name": "4x-UltraSharp.safetensors"}},
        "3": {"class_type": "ImageUpscaleWithModel", "inputs": {"upscale_model": ["2", 0], "image": ["1", 0]}},
        "4": {"class_type": "SaveImage", "inputs": {"images": ["3", 0], "filename_prefix": "ti84/diff_up"}},
    }


if __name__ == "__main__":
    os.makedirs("wf", exist_ok=True)
    if len(sys.argv) > 2 and sys.argv[1] == "up":
        json.dump(upscale(sys.argv[2]), open("wf/upscale.json", "w"), indent=1)
        print("wf/upscale.json")
    else:
        for dn in (0.30, 0.38, 0.45):
            for seed in (84001, 84002):
                p = f"wf/img2img_d{int(dn * 100)}_s{seed}.json"
                json.dump(img2img(dn, seed), open(p, "w"), indent=1)
                print(p)
        json.dump(img2img(0.30, 84001), open("wf_img2img.json", "w"), indent=1)
