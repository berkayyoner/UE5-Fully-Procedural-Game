/*************************************************************************
* ADOBE CONFIDENTIAL
* ___________________
*
*  Copyright 2015 Adobe Systems Incorporated
*  All Rights Reserved.
*
* NOTICE:  All information contained herein is, and remains
* the property of Adobe Systems Incorporated and its suppliers,
* if any.  The intellectual and technical concepts contained
* herein are proprietary to Adobe Systems Incorporated and its
* suppliers and are protected by all applicable intellectual property laws,
* including trade secret and or copyright laws.
* Dissemination of this information or reproduction of this material
* is strictly forbidden unless prior written permission is obtained
* from Adobe Systems Incorporated.
**************************************************************************/
import{analytics as e}from"../../../common/analytics.js";import{loggingApi as r}from"../../../common/loggingApi.js";import{dcTabStorage as o}from"../tab-storage.js";import{getSearchParams as t}from"../../../common/util.js";const i=["createpdf","compresspdf","html-to-pdf"];export const getAcrobatPromotionSource=()=>{const e=o.getItem("search")||"",r=new URLSearchParams(e).get("pdfurl");return t(r).get("acrobatPromotionSource")};export function sendFileBufferToViewerForDirectFlow(r,o,t,i){t.then((e=>{const t=e.downLoadEndTime,n=e.buffer,a=e.buffer.byteLength,c=e.mimeType,s=e.pdffilename||"document.doc";r.executeDirectVerb("executeDirectVerb",n,a,s,i,t,c,o.origin)})).catch((r=>{e.event("DCBrowserExt:Viewer:Error:DirectFlow:FileDownload:Failed",{source:getAcrobatPromotionSource()})}))}export async function handlePostPDFConversionInDirectFlow(e,r,t){o.setItem("acrobatPromotionWorkflow","preview"),r+=`?pdfurl=${encodeURIComponent(t)}&pdffilename=${encodeURIComponent(e.data.fileName)}`;const i=(await chrome.tabs.getCurrent())?.id;chrome.tabs.update(i,{url:r})}const n=async()=>{chrome.runtime.sendMessage({main_op:"get-welcome-pdf-url"}).then((async e=>{const r=(await chrome.tabs.getCurrent())?.id;chrome.tabs.update(r,{url:e})}))};export async function previewOriginalPDF(){try{const e=o.getItem("search");if(e){o.setItem("acrobatPromotionWorkflow","preview");const r=new URLSearchParams(e);r.set("acrobatPromotionWorkflow","preview");const t=`${chrome.runtime.getURL("viewer.html")}?${r.toString()}`,i=(await chrome.tabs.getCurrent())?.id;chrome.tabs.update(i,{url:t})}else await n(),r.error({message:"[Compress PDF Direct Flow] Error in previewOriginalPDF. Missing 'search' parameter in tab storage",error:"originalSearch is null or undefined"})}catch(e){await n(),r.error({message:"[Compress PDF Direct Flow] Error in previewOriginalPDF. Unexpected error while opening Acrobat preview viewer.",error:e})}}export const isDirectFlowWithoutPreview=()=>{const e=o.getItem("acrobatPromotionWorkflow");return i.includes(e)};export async function handleUpsellCloseInDirectFlow(e){i.includes(e?.data?.workflow)&&(o.removeItem("acrobatPromotionWorkflow"),"compresspdf"===e?.data?.workflow?await previewOriginalPDF():await n())}export function isPreviewPostDirectFlow(){return"preview"===o.getItem("acrobatPromotionWorkflow")}